// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
Generates a .dat.actual file for the tracepoint API calls in the sample.
Verifies that the resulting .dat.actual file is the same as the .dat.expected file.
*/

#include "TestCommon.h"
#include <stdio.h>
#include <unistd.h>
#include <exception>
#include <string>

extern char const* g_interceptorFileName;

static std::string
LoadFile(char const* filename)
{
    FILE* file = fopen(filename, "rb");
    if (!file)
    {
        fprintf(stdout, "Failed to open file: %s\n", filename);
        throw std::exception();
    }

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    std::string result(size, '\0');
    if (fread(&result[0], 1, size, file) != size)
    {
        fclose(file);
        fprintf(stdout, "Failed to read file: %s\n", filename);
        throw std::exception();
    }

    fclose(file);
    return result;
}

static std::string
MakeDatName(char const* baseDir, char const* suffix)
{
    static char const* const DatName=
        "EventHeaderInterceptor"
#if __BYTE_ORDER == __LITTLE_ENDIAN
        "LE"
#elif __BYTE_ORDER == __BIG_ENDIAN
        "BE"
#endif
#if __SIZEOF_POINTER__ == 8
        "64"
#elif __SIZEOF_POINTER__ == 4
        "32"
#endif
        ".dat"
        ;

    std::string result = baseDir;
    result += "/";
    result += DatName;
    result += suffix;
    return result;
}

int
main(int argc, char* argv[])
{
    // If an argument is provided, use it as the base directory. Otherwise, use ".".
    auto const baseDir = argc > 1 ? argv[1] : ".";

    try
    {
        std::string const actualName = MakeDatName(baseDir, ".actual");
        std::string const expectedName = MakeDatName(baseDir, ".expected");

        g_interceptorFileName = actualName.c_str();
        fprintf(stdout, "Writing to %s\n", g_interceptorFileName);
        remove(g_interceptorFileName);

        int err = TestCommon();
        if (err != 0)
        {
            return err;
        }

        auto const actualDat = LoadFile(actualName.c_str());
        auto const expectedDat = LoadFile(expectedName.c_str());
        if (actualDat != expectedDat)
        {
            fprintf(stdout, "ERROR: %s != %s\n", actualName.c_str(), expectedName.c_str());
            return 1;
        }
    }
    catch (std::exception const& ex)
    {
        fprintf(stdout, "ERROR: %s\n", ex.what());
        return 1;
    }

    return 0;
}
