// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// ===========================================================================
// File: perfmap.h
//
#ifndef PERFPID_H
#define PERFPID_H

#include "sstring.h"
#include "fstream.h"
#include "volatile.h"

// Generates a perfmap file.

enum class PerfMapStubType
{
    Block,
    IndividualWithinBlock,
    Individual
};

class PerfMap
{
private:
    static Volatile<bool> s_enabled;

    // The one and only PerfMap for the process.
    static PerfMap * s_Current;

    // Indicates whether optimization tiers should be shown for methods in perf maps
    static bool s_ShowOptimizationTiers;

    // Indicate current stub granularity rules
    static bool s_GroupStubsOfSameType;
    static bool s_IndividualAllocationStubReporting;

    // Set to true if an error is encountered when writing to the file.
    static unsigned s_StubsMapped;

    static CrstStatic s_csPerfMap;

    // The file stream to write the map to.
    CFileStream * m_FileStream;

    // Set to true if an error is encountered when writing to the file.
    bool m_ErrorEncountered;

    // Construct a new map
    PerfMap();

    // Open a perfmap map for the specified pid
    void OpenFileForPid(int pid, const char* basePath);

    // Write a line to the map file.
    void WriteLine(SString & line);

    // Default to /tmp or use DOTNET_PerfMapJitDumpPath if set
    static const char* InternalConstructPath();

    static void InitializeConfiguration();

protected:
    // Open the perf map file for write.
    void OpenFile(SString& path);

    // Does the actual work to log a method to the map.
    void LogMethod(MethodDesc * pMethod, PCODE pCode, size_t codeSize, const char *optimizationTier);

    // Get the image signature and store it as a string.
    static void GetNativeImageSignature(PEAssembly * pPEAssembly, CHAR * pszSig, unsigned int nSigSize);

public:
    // Clean-up resources.
    ~PerfMap();

    enum class PerfMapType
    {
        DISABLED = 0,
        ALL      = 1,
        JITDUMP  = 2,
        PERFMAP  = 3
    };

    static bool IsEnabled()
    {
        LIMITED_METHOD_CONTRACT;

        return s_enabled;
    }

    // Initialize the map for the current process.
    static void Initialize();

    static void Enable(PerfMapType type, bool sendExisting);

    static void LogJITCompiledMethod(MethodDesc * pMethod, PCODE pCode, size_t codeSize, PrepareCodeConfig *pConfig);

    // Log a pre-compiled method to the map.
    static void LogPreCompiledMethod(MethodDesc * pMethod, PCODE pCode);

    // Log a set of stub to the map.
    static void LogStubs(const char* stubType, const char* stubOwner, PCODE pCode, size_t codeSize, PerfMapStubType stubAllocationType);

    // Close the map and flush any remaining data.
    static void Disable();

    static bool LowGranularityStubs() { return !s_IndividualAllocationStubReporting; }
};
#endif // PERFPID_H
