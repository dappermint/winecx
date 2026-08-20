/*
 * NTDLL error handling
 *
 * Copyright 2000 Alexandre Julliard
 * Copyright 2002 Andriy Palamarchuk
 * Copyright 2010 André Hentschel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "ntstatus.h"
#include "windef.h"
#include "winternl.h"
#include "winerror.h"
#include "error.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ntdll);

#ifdef __x86_64__
/* Mono's jit compiles Marshal.GetLastWin32Error() to a bare `mov reg,%gs:0x68`
 * instead of a call, so the last error has to be readable straight off %gs.
 * That is free on hosts where %gs is the TEB, but macOS keeps its own thread
 * data there and only a handful of TEB fields are mirrored into it, so write
 * this one as well. 0x68 is the pthread slot gmtime() would use; loader.c hooks
 * gmtime() away for that reason, the same way it already does for localtime()
 * over the PEB at 0x60. */
static inline void mirror_last_error( DWORD err )
{
    /* a 4-byte store: where %gs really is the TEB this is the very field we
     * just wrote, and anything wider would take CountOfOwnedCriticalSections
     * with it. init_syscall_frame clears the whole slot once per thread. */
    __asm__ volatile( "movl %0,%%gs:0x68" :: "r" (err) );
}
#else
static inline void mirror_last_error( DWORD err )
{
}
#endif

/**************************************************************************
 *           RtlNtStatusToDosErrorNoTeb (NTDLL.@)
 *
 * Convert an NTSTATUS code to a Win32 error code.
 *
 * PARAMS
 *  status [I] Nt error code to map.
 *
 * RETURNS
 *  The mapped Win32 error code, or ERROR_MR_MID_NOT_FOUND if there is no
 *  mapping defined.
 */
ULONG WINAPI RtlNtStatusToDosErrorNoTeb( NTSTATUS status )
{
    ULONG ret;

    if (!status || (status & 0x20000000)) return status;

    /* 0xd... is equivalent to 0xc... */
    if ((status & 0xf0000000) == 0xd0000000) status &= ~0x10000000;

    /* now some special cases */
    if (HIWORD(status) == 0xc001 || HIWORD(status) == 0x8007 || HIWORD(status) == 0xc007)
        return LOWORD( status );

    ret = map_status( status );
    if (ret == ERROR_MR_MID_NOT_FOUND && status != STATUS_MESSAGE_NOT_FOUND)
        WARN( "no mapping for %08lx\n", status );
    return ret;
}

/**************************************************************************
 *           RtlNtStatusToDosError (NTDLL.@)
 *
 * Convert an NTSTATUS code to a Win32 error code.
 *
 * PARAMS
 *  status [I] Nt error code to map.
 *
 * RETURNS
 *  The mapped Win32 error code, or ERROR_MR_MID_NOT_FOUND if there is no
 *  mapping defined.
 */
ULONG WINAPI RtlNtStatusToDosError( NTSTATUS status )
{
    NtCurrentTeb()->LastStatusValue = status;
    return RtlNtStatusToDosErrorNoTeb( status );
}

/**********************************************************************
 *      RtlGetLastNtStatus (NTDLL.@)
 *
 * Get the current per-thread status.
 */
NTSTATUS WINAPI RtlGetLastNtStatus(void)
{
    return NtCurrentTeb()->LastStatusValue;
}

/**********************************************************************
 *      RtlGetLastWin32Error (NTDLL.@)
 *
 * Get the current per-thread error value set by a system function or the user.
 *
 * PARAMS
 *  None.
 *
 * RETURNS
 *  The current error value for the thread, as set by SetLastWin32Error() or SetLastError().
 */
DWORD WINAPI RtlGetLastWin32Error(void)
{
    return NtCurrentTeb()->LastErrorValue;
}

/***********************************************************************
 *      RtlSetLastWin32Error (NTDLL.@)
 *      RtlRestoreLastWin32Error (NTDLL.@)
 *
 * Set the per-thread error value.
 *
 * PARAMS
 *  err [I] The new error value to set
 *
 * RETURNS
 *  Nothing.
 */
void WINAPI RtlSetLastWin32Error( DWORD err )
{
    NtCurrentTeb()->LastErrorValue = err;
    mirror_last_error( err );
}

/***********************************************************************
 *      RtlSetLastWin32ErrorAndNtStatusFromNtStatus (NTDLL.@)
 *
 * Set the per-thread status and error values.
 *
 * PARAMS
 *  err [I] The new status value to set
 *
 * RETURNS
 *  Nothing.
 */
void WINAPI RtlSetLastWin32ErrorAndNtStatusFromNtStatus( NTSTATUS status )
{
    NtCurrentTeb()->LastErrorValue = RtlNtStatusToDosError( status );
    mirror_last_error( NtCurrentTeb()->LastErrorValue );
}
