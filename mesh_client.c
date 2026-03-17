#include <metacall/metacall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
#include <pthread.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_EQ(actual, expected, msg) do { \
	if ((actual) != (expected)) { \
		printf("    ✗ FAIL: %s\n", msg); \
		printf("      Expected: %d, Got: %d\n", (int)(expected), (int)(actual)); \
		printf("      at %s:%d\n", __FILE__, __LINE__); \
		return 1; \
	} else { \
		printf("    ✓ PASS: %s\n", msg); \
	} \
} while(0)

#define ASSERT_DOUBLE_EQ(actual, expected, msg) do { \
	double _a = (actual), _e = (expected); \
	if (_a < _e - 0.01 || _a > _e + 0.01) { \
		printf("    ✗ FAIL: %s\n", msg); \
		printf("      Expected: %.2f, Got: %.2f\n", _e, _a); \
		printf("      at %s:%d\n", __FILE__, __LINE__); \
		return 1; \
	} else { \
		printf("    ✓ PASS: %s\n", msg); \
	} \
} while(0)

#define RUN_TEST(test_func) do { \
	tests_run++; \
	printf("\n── TEST %d: %s ──\n", tests_run, #test_func); \
	if (test_func() == 0) { \
		tests_passed++; \
		printf("  Result: PASSED ✅\n"); \
	} else { \
		tests_failed++; \
		printf("  Result: FAILED ❌\n"); \
	} \
} while(0)

// Wait for an atomic counter to reach target, with timeout (ms).
static int wait_for_count(atomic_int *counter, int target, int timeout_ms)
{
	int waited = 0;

	while (atomic_load(counter) < target && waited < timeout_ms)
	{
		usleep(10000); /* 10ms */
		waited += 10;
	}

	return atomic_load(counter) >= target;
}

// Shared Async State
static atomic_int g_resolved;
static atomic_int g_rejected;
static double g_last_result;

// Per-call context: tracks expected result for verification
typedef struct
{
	int call_id;
	double expected;
} call_context;

static atomic_int g_mismatches;

// Resolve callback that verifies the result matches expected value
void *on_resolve_verified(void *result, void *data)
{
	call_context *ctx = (call_context *)data;

	if (result != NULL)
	{
		double actual = -1;
		int type_id = metacall_value_id(result);

		if (type_id == METACALL_DOUBLE)
			actual = metacall_value_to_double(result);
		else if (type_id == METACALL_FLOAT)
			actual = (double)metacall_value_to_float(result);

		if (actual < ctx->expected - 0.01 || actual > ctx->expected + 0.01)
		{
			printf("    [MISMATCH] call_id=%d expected=%.1f got=%.1f\n",
				   ctx->call_id, ctx->expected, actual);
			atomic_fetch_add(&g_mismatches, 1);
		}

		metacall_value_destroy(result);
	}

	atomic_fetch_add(&g_resolved, 1);
	return NULL;
}

// Generic resolve callback — increments counter, stores result
void *on_resolve(void *result, void *data)
{
	const char *label = (data != NULL) ? (const char *)data : "unknown";

	if (result != NULL)
	{
		int type_id = metacall_value_id(result);

		if (type_id == METACALL_DOUBLE)
		{
			g_last_result = metacall_value_to_double(result);
			printf("    [RESOLVE] %s → %.1f\n", label, g_last_result);
		}
		else if (type_id == METACALL_FLOAT)
		{
			g_last_result = (double)metacall_value_to_float(result);
			printf("    [RESOLVE] %s → %.1f\n", label, g_last_result);
		}
		else if (type_id == METACALL_LONG)
		{
			g_last_result = (double)metacall_value_to_long(result);
			printf("    [RESOLVE] %s → %.0f\n", label, g_last_result);
		}
		else
		{
			printf("    [RESOLVE] %s → (type=%d)\n", label, type_id);
		}

		metacall_value_destroy(result);
	}
	else
	{
		printf("    [RESOLVE] %s → NULL\n", label);
	}

	atomic_fetch_add(&g_resolved, 1);
	return NULL;
}

/* Generic reject callback — increments counter */
void *on_reject(void *error, void *data)
{
	const char *label = (data != NULL) ? (const char *)data : "unknown";

	if (error != NULL)
	{
		printf("    [REJECT] %s → %s\n", label, metacall_value_to_string(error));
		metacall_value_destroy(error);
	}
	else
	{
		printf("    [REJECT] %s → NULL\n", label);
	}

	atomic_fetch_add(&g_rejected, 1);
	return NULL;
}

// Test 1: Single Async Call (Baseline)
static int test_single_async(void)
{
	atomic_store(&g_resolved, 0);
	atomic_store(&g_rejected, 0);
	g_last_result = -1;

	void *args[] = {
		metacall_value_create_double(100),
		metacall_value_create_double(200)
	};

	metacall_await_s("slow_add", args, 2, on_resolve, on_reject, (void *)"single_async");

	metacall_value_destroy(args[0]);
	metacall_value_destroy(args[1]);

	int reached = wait_for_count(&g_resolved, 1, 5000);

	ASSERT_EQ(reached, 1, "Callback should fire within 5s");
	ASSERT_EQ(atomic_load(&g_resolved), 1, "Exactly 1 resolve");
	ASSERT_EQ(atomic_load(&g_rejected), 0, "No rejects");
	ASSERT_DOUBLE_EQ(g_last_result, 300.0, "slow_add(100, 200) == 300");

	return 0;
}

// Test 2: Rapid Fire (20 async calls in a tight loop)
#define RAPID_FIRE_COUNT 20

static call_context rf_contexts[RAPID_FIRE_COUNT];

static int test_rapid_fire(void)
{
	atomic_store(&g_resolved, 0);
	atomic_store(&g_rejected, 0);
	atomic_store(&g_mismatches, 0);

	printf("    Dispatching %d async calls...\n", RAPID_FIRE_COUNT);

	for (int i = 0; i < RAPID_FIRE_COUNT; i++)
	{
		double a = (double)i;
		double b = 1.0;

		rf_contexts[i].call_id = i;
		rf_contexts[i].expected = a + b;

		void *args[] = {
			metacall_value_create_double(a),
			metacall_value_create_double(b)
		};

		metacall_await_s("add", args, 2, on_resolve_verified, on_reject, &rf_contexts[i]);

		metacall_value_destroy(args[0]);
		metacall_value_destroy(args[1]);
	}

	printf("    All %d calls dispatched, waiting for callbacks...\n", RAPID_FIRE_COUNT);

	int reached = wait_for_count(&g_resolved, RAPID_FIRE_COUNT, 10000);

	int resolved = atomic_load(&g_resolved);
	int rejected = atomic_load(&g_rejected);
	int mismatches = atomic_load(&g_mismatches);

	printf("    Resolved: %d/%d, Rejected: %d, Mismatches: %d\n",
		   resolved, RAPID_FIRE_COUNT, rejected, mismatches);

	ASSERT_EQ(reached, 1, "All callbacks should fire within 10s");
	ASSERT_EQ(resolved, RAPID_FIRE_COUNT, "All calls should resolve");
	ASSERT_EQ(rejected, 0, "No rejects");
	ASSERT_EQ(mismatches, 0, "All results should match expected values");

	return 0;
}

// Test 3: Concurrent Producers (multiple threads calling await)
#define NUM_THREADS     4
#define CALLS_PER_THREAD 10
#define TOTAL_CONCURRENT (NUM_THREADS * CALLS_PER_THREAD)

typedef struct
{
	int thread_id;
	call_context *contexts; // pointer to this thread's slice of contexts
} producer_data;

static void *producer_thread(void *arg)
{
	producer_data *pd = (producer_data *)arg;

	for (int i = 0; i < CALLS_PER_THREAD; i++)
	{
		double a = (double)(pd->thread_id * 100 + i);
		double b = 1.0;

		pd->contexts[i].call_id = pd->thread_id * 100 + i;
		pd->contexts[i].expected = a + b;

		void *args[] = {
			metacall_value_create_double(a),
			metacall_value_create_double(b)
		};

		metacall_await_s("add", args, 2, on_resolve_verified, on_reject, &pd->contexts[i]);

		metacall_value_destroy(args[0]);
		metacall_value_destroy(args[1]);
	}

	printf("    Thread %d: dispatched %d calls\n", pd->thread_id, CALLS_PER_THREAD);
	return NULL;
}

static int test_concurrent_producers(void)
{
	atomic_store(&g_resolved, 0);
	atomic_store(&g_rejected, 0);
	atomic_store(&g_mismatches, 0);

	pthread_t threads[NUM_THREADS];
	producer_data data[NUM_THREADS];
	call_context all_contexts[TOTAL_CONCURRENT];

	printf("    Launching %d threads × %d calls = %d total\n",
		   NUM_THREADS, CALLS_PER_THREAD, TOTAL_CONCURRENT);

	for (int i = 0; i < NUM_THREADS; i++)
	{
		data[i].thread_id = i;
		data[i].contexts = &all_contexts[i * CALLS_PER_THREAD];
		pthread_create(&threads[i], NULL, producer_thread, &data[i]);
	}

	for (int i = 0; i < NUM_THREADS; i++)
	{
		pthread_join(threads[i], NULL);
	}

	printf("    All threads done, waiting for callbacks...\n");

	int reached = wait_for_count(&g_resolved, TOTAL_CONCURRENT, 15000);

	int resolved = atomic_load(&g_resolved);
	int rejected = atomic_load(&g_rejected);
	int mismatches = atomic_load(&g_mismatches);

	printf("    Resolved: %d/%d, Rejected: %d, Mismatches: %d\n",
		   resolved, TOTAL_CONCURRENT, rejected, mismatches);

	ASSERT_EQ(reached, 1, "All callbacks should fire within 15s");
	ASSERT_EQ(resolved, TOTAL_CONCURRENT, "All concurrent calls should resolve");
	ASSERT_EQ(rejected, 0, "No rejects");
	ASSERT_EQ(mismatches, 0, "All results should match expected values");

	return 0;
}

// Test 4: Mixed Sync + Async
static int test_mixed_sync_async(void)
{
	atomic_store(&g_resolved, 0);
	atomic_store(&g_rejected, 0);

	void *async_args[] = {
		metacall_value_create_double(100),
		metacall_value_create_double(200)
	};
	metacall_await_s("slow_add", async_args, 2, on_resolve, on_reject, (void *)"mixed_async");
	metacall_value_destroy(async_args[0]);
	metacall_value_destroy(async_args[1]);

    void *sync_args1[] = {
		metacall_value_create_double(10),
		metacall_value_create_double(20)
	};
	void *r1 = metacallv("add", sync_args1);
	double sync_result1 = metacall_value_to_double(r1);
	printf("    Sync add(10, 20) = %.0f\n", sync_result1);
	metacall_value_destroy(sync_args1[0]);
	metacall_value_destroy(sync_args1[1]);
	metacall_value_destroy(r1);

	void *sync_args2[] = {
		metacall_value_create_double(7),
		metacall_value_create_double(8)
	};
	void *r2 = metacallv("multiply", sync_args2);
	double sync_result2 = metacall_value_to_double(r2);
	printf("    Sync multiply(7, 8) = %.0f\n", sync_result2);
	metacall_value_destroy(sync_args2[0]);
	metacall_value_destroy(sync_args2[1]);
	metacall_value_destroy(r2);

	int reached = wait_for_count(&g_resolved, 1, 5000);

	ASSERT_DOUBLE_EQ(sync_result1, 30.0, "Sync add(10, 20) == 30");
	ASSERT_DOUBLE_EQ(sync_result2, 56.0, "Sync multiply(7, 8) == 56");
	ASSERT_EQ(reached, 1, "Async callback should fire");
	ASSERT_DOUBLE_EQ(g_last_result, 300.0, "Async slow_add(100, 200) == 300");

	return 0;
}

// Test 5: Shutdown mid-transfer
// Fires a slow async call, then immediately destroys — verifies graceful drain
static int test_shutdown_mid_transfer(void)
{
	atomic_store(&g_resolved, 0);
	atomic_store(&g_rejected, 0);

	printf("    Re-initializing for isolated shutdown test...\n");

	// We need a fresh metacall instance for this test
	metacall_destroy();
	metacall_initialize();

	const char *scripts[] = { "client/hub.url" };
	if (metacall_load_from_file("rpc", scripts, 1, NULL) != 0)
	{
		printf("    Could not reconnect to server\n");
		return 1;
	}

	// Fire slow_add (takes 500ms on server)
	void *args[] = {
		metacall_value_create_double(50),
		metacall_value_create_double(75)
	};
	metacall_await_s("slow_add", args, 2, on_resolve, on_reject, (void *)"shutdown_mid");
	metacall_value_destroy(args[0]);
	metacall_value_destroy(args[1]);

	printf("    Async call dispatched, calling destroy immediately...\n");

	// Destroy while transfer is in-flight — should drain gracefully
	metacall_destroy();

	printf("    Destroy returned (no hang, no crash)\n");

	int resolved = atomic_load(&g_resolved);
	printf("    Callbacks fired during drain: %d\n", resolved);

	ASSERT_EQ(resolved, 1, "Callback should fire during drain");
	ASSERT_DOUBLE_EQ(g_last_result, 125.0, "slow_add(50, 75) == 125");

	// Re-initialize for remaining tests
	metacall_initialize();
	if (metacall_load_from_file("rpc", scripts, 1, NULL) != 0)
	{
		printf("    Could not reconnect after shutdown test\n");
		return 1;
	}

	return 0;
}

// Test 6: Empty shutdown
// Destroy with zero async calls — poll thread should exit cleanly
static int test_empty_shutdown(void)
{
	printf("    Destroying with no async calls pending...\n");

	metacall_destroy();

	printf("    Destroy returned (no hang)\n");

	// Re-initialize to prove everything is still okay
	metacall_initialize();

	const char *scripts[] = { "client/hub.url" };
	if (metacall_load_from_file("rpc", scripts, 1, NULL) != 0)
	{
		printf("    Could not reconnect after empty shutdown\n");
		return 1;
	}

	// Quick sanity check — sync call still works
	void *args[] = {
		metacall_value_create_double(5),
		metacall_value_create_double(3)
	};
	void *r = metacallv("add", args);
	double result = metacall_value_to_double(r);
	printf("    Post-restart sync add(5, 3) = %.0f\n", result);
	metacall_value_destroy(args[0]);
	metacall_value_destroy(args[1]);
	metacall_value_destroy(r);

	ASSERT_DOUBLE_EQ(result, 8.0, "Sync call works after re-init");

	return 0;
}

int main(void)
{
	printf("╔══════════════════════════════════════════╗\n");
	printf("║   Async RPC Test Harness                 ║\n");
	printf("║   MPSC Lock-Free Queue Validation        ║\n");
	printf("╚══════════════════════════════════════════╝\n");

	metacall_initialize();

	const char *scripts[] = { "client/hub.url" };

	if (metacall_load_from_file("rpc", scripts, 1, NULL) != 0)
	{
		printf("FATAL: Failed to load RPC script. Is the server accessible via the URL in client/hub.url?\n");
		metacall_destroy();
		return 1;
	}

	printf("Server connected. Running tests...\n");

	RUN_TEST(test_single_async);
	RUN_TEST(test_rapid_fire);
	RUN_TEST(test_concurrent_producers);
	RUN_TEST(test_mixed_sync_async);
	RUN_TEST(test_shutdown_mid_transfer);
	RUN_TEST(test_empty_shutdown);

	/* Summary */
	printf("\n══════════════════════════════════════════\n");
	printf("  Results: %d/%d passed", tests_passed, tests_run);

	if (tests_failed > 0)
	{
		printf(", %d FAILED ❌\n", tests_failed);
	}
	else
	{
		printf(" ✅ All clear!\n");
	}

	printf("══════════════════════════════════════════\n");

	/* Graceful shutdown */
	sleep(1);
	metacall_destroy();

	return tests_failed > 0 ? 1 : 0;
}
