/*
 * Simple RPC client for Function Mesh PoC
 * Tests both sync and async function calls
 */

#include <metacall/metacall.h>
#include <stdio.h>
#include <unistd.h>

/* Async callbacks */
void *on_resolve(void *result, void *data)
{
    if (result != NULL)
    {
        int type_id = metacall_value_id(result);
        printf("[ASYNC] Type: %d, Result: ", type_id);
        
        if (type_id == METACALL_DOUBLE)
            printf("%.0f\n", metacall_value_to_double(result));
        else if (type_id == METACALL_FLOAT)
            printf("%.0f\n", (double)metacall_value_to_float(result));
        else if (type_id == METACALL_LONG)
            printf("%ld\n", metacall_value_to_long(result));
        else if (type_id == METACALL_INT)
            printf("%d\n", metacall_value_to_int(result));
        else
            printf("(unknown type)\n");
        
        metacall_value_destroy(result);
    }
    else
    {
        printf("[ASYNC] Result is NULL\n");
    }
    return NULL;
}

void *on_reject(void *error, void *data)
{
    printf("[ASYNC] Error!\n");
    return NULL;
}

int main()
{
    metacall_initialize();

    const char *scripts[] = {"mesh_server.url"};
    if (metacall_load_from_file("rpc", scripts, 1, NULL) != 0) {
        printf("Failed! Is mesh_server.py running?\n");
        return 1;
    }

    printf("=== SYNC CALLS ===\n");

    /* Sync: add(10, 20) */
    void *args1[] = {metacall_value_create_double(10), metacall_value_create_double(20)};
    void *r1 = metacallv("add", args1);
    printf("add(10, 20) = %.0f\n", metacall_value_to_double(r1));
    metacall_value_destroy(args1[0]);
    metacall_value_destroy(args1[1]);
    metacall_value_destroy(r1);

    /* Sync: multiply(7, 8) */
    void *args2[] = {metacall_value_create_double(7), metacall_value_create_double(8)};
    void *r2 = metacallv("multiply", args2);
    printf("multiply(7, 8) = %.0f\n", metacall_value_to_double(r2));
    metacall_value_destroy(args2[0]);
    metacall_value_destroy(args2[1]);
    metacall_value_destroy(r2);

    printf("\n=== ASYNC CALL ===\n");

    /* Async: slow_add(100, 200) - returns immediately */
    void *args3[] = {metacall_value_create_double(100), metacall_value_create_double(200)};
    printf("Calling slow_add(100, 200) async...\n");
    metacall_await_s("slow_add", args3, 2, on_resolve, on_reject, NULL);
    printf("Call dispatched! (non-blocking)\n");
    metacall_value_destroy(args3[0]);
    metacall_value_destroy(args3[1]);

    /* Wait for async result */
    printf("Waiting for async result...\n");
    sleep(2);

    printf("\n=== DONE ===\n");
    metacall_destroy();
    return 0;
}
