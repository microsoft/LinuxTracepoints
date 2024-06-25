// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
tracepoint-file is an implementation of the tracepoint.h interface that appends
events to a file.

This is part of the eventheader-interceptor-sample program, demonstrating how
linking against tracepoint-file.o instead of libtracepoint.a causes the program
to append events to a file instead of sending them to the Linux user_events
facility.
*/

#pragma once
#ifndef _included_tracepoint_file_h
#define _included_tracepoint_file_h 1

/*
Configuration point: Controls the name of the file that will be written.
The default value of this variable is "EventHeaderInterceptor[endian][bits].dat",
where [endian] is "LE" or "BE" and [bits] is "32" or "64".

Each open provider maintains a reference count to the file. The file is opened when
you call tracepoint_open_provider and the reference count increments to 1. The file
is closed when you call tracepoint_close_provider and the reference count decrements
to 0.

Changes to this variable take effect when tracepoint_open_provider is called and
the reference count increments to 1.
*/
#ifdef __cplusplus
extern "C"
#else
extern
#endif
char const* g_interceptorFileName;

#endif // _included_tracepoint_file_h
