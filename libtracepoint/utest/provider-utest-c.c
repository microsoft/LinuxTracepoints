#define C_OR_CPP C
#include "tpp-utest.h"

#include <stdio.h>

int TestC(void)
{
    int err = TPP_REGISTER_PROVIDER(TestProvider);
    printf("TestProviderC register: %d\n", err);

    int ok = TestCommon();

    TPP_UNREGISTER_PROVIDER(TestProvider);
    return ok != 0 && err == 0;
}

void PrintErr(char const* operation, int err)
{
    printf("%s: %d\n", operation, err);
}

#include <errno.h>

int main()
{
    TestC();
    TestCpp();

    if (EBADF != 9)
    {
        printf("ERROR: EBADF != 9\n");
        return 1;
    }

    return 0;
}
