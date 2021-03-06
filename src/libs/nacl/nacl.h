/*
===========================================================================
Daemon BSD Source Code
Copyright (c) 2013-2014, Daemon Developers
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Daemon developers nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL DAEMON DEVELOPERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
===========================================================================
*/

#ifndef NACL_H_
#define NACL_H_

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <vector>

namespace NaCl {

// Operating system handle type
#ifdef _WIN32
// HANDLE is defined as void* in windows.h
typedef void* OSHandleType;
static const OSHandleType INVALID_HANDLE = NULL;
#else
typedef int OSHandleType;
static const OSHandleType INVALID_HANDLE = -1;
#endif

// noexcept keyword support
#ifdef __GNUC__
#define NACL_NOEXCEPT noexcept
#else
#define NACL_NOEXCEPT
#endif

// Maximum number of bytes that can be sent in a message
static const size_t MSG_MAX_BYTES = 128 << 10;

// Maximum number of handles that can be sent in a message
static const size_t MSG_MAX_HANDLES = 8;

// File modes
enum FileOpenMode {
	MODE_READ,
	MODE_WRITE,
	MODE_RW,
	MODE_WRITE_APPEND,
	MODE_RW_APPEND
};

// Pointer to a shared-memory region
class SharedMemoryPtr {
public:
	SharedMemoryPtr()
		: addr(nullptr) {}
	SharedMemoryPtr(SharedMemoryPtr&& other) NACL_NOEXCEPT
		: addr(other.addr), size(other.size)
	{
		other.addr = nullptr;
	}
	SharedMemoryPtr& operator=(SharedMemoryPtr&& other) NACL_NOEXCEPT
	{
		std::swap(addr, other.addr);
		std::swap(size, other.size);
		return *this;
	}
	~SharedMemoryPtr()
	{
		Close();
	}

	explicit operator bool() const
	{
		return addr != nullptr;
	}

	// Get the mapping parameters
	void* Get() const
	{
		return addr;
	}
	size_t GetSize() const
	{
		return size;
	}

	// Release the memory mapping
	void Close();

private:
	friend class IPCHandle;

	void* addr;
	size_t size;
};

// IPC object handle, which can refer to either:
// - A message-based socket
// - A shared-memory region
// - An open file
class IPCHandle {
public:
	IPCHandle()
		: handle(INVALID_HANDLE) {}
	~IPCHandle()
	{
		Close();
	}
	IPCHandle(IPCHandle&& other) NACL_NOEXCEPT
	{
		handle = other.handle;
		other.handle = INVALID_HANDLE;
#ifndef __native_client__
		type = other.type;
		size = other.size;
#endif
	}
	IPCHandle& operator=(IPCHandle&& other) NACL_NOEXCEPT
	{
		std::swap(handle, other.handle);
#ifndef __native_client__
		std::swap(type, other.type);
		std::swap(size, other.size);
#endif
		return *this;
	}

	// Check if the handle is valid
	explicit operator bool() const
	{
		return handle != INVALID_HANDLE;
	}

	// Close the handle
	void Close();

	// Release ownership of the underlying OS handle so that it isn't closed
	// when the IPCHandle object is destroyed.
	OSHandleType ReleaseHandle()
	{
		OSHandleType out = handle;
		handle = INVALID_HANDLE;
		return out;
	}

	// Get the underlying OS handle
	OSHandleType GetOSHandle() const
	{
		return handle;
	}

	// Send a message through the socket
	// Returns the number of bytes sent or -1 on error
	bool SendMsg(const void* data, size_t len) const;

	// Recieve a message from the socket, will block until a message arrives.
	// Returns the number of bytes sent or -1 on error
	bool RecvMsg(std::vector<char>& buffer) const;

	// Map a shared memory region into memory
	// Returns a pointer to the memory mapping or NULL on error
	// The mapping remains valid even after the handle is closed
	SharedMemoryPtr Map() const;

private:
	OSHandleType handle;

	friend bool InternalSendMsg(OSHandleType handle, const void* data, size_t len, IPCHandle* const* handles, size_t num_handles);
	friend bool InternalRecvMsg(OSHandleType handle, std::vector<char>& buffer, std::vector<IPCHandle>& handles);
	friend bool SocketPair(IPCHandle& first, IPCHandle& second);
	friend IPCHandle CreateSharedMemory(size_t size);
	friend IPCHandle WrapFileHandle(OSHandleType handle, FileOpenMode mode);

	// Information only required on the host side for handle transfer protocol
#ifndef __native_client__
	enum {
		TYPE_SOCKET,
		TYPE_SHM,
		TYPE_FILE
	};
	int type;
	union {
		uint64_t size;
		int32_t flags;
	};
#endif
};

// Create a pair of sockets which are linked to each other.
// Returns false on error
bool SocketPair(IPCHandle& first, IPCHandle& second);

// Allocate a shared memory region of the specified size.
IPCHandle CreateSharedMemory(size_t size);

// Wrap an open file handle in an IPCHandle
IPCHandle WrapFileHandle(OSHandleType handle, FileOpenMode mode);

// Root socket of a module, created by the parent process. This is the only
// socket which can transfer IPCHandles in messages.
class RootSocket {
public:
	// Note that the socket is not closed in the destructor. This is done to
	// allow the socket to be re-created at any time using GetRootSocket.
	RootSocket()
		: handle(INVALID_HANDLE) {}
	RootSocket(RootSocket&& other) NACL_NOEXCEPT
		: handle(other.handle) {}
	RootSocket& operator=(RootSocket&& other) NACL_NOEXCEPT
	{
		handle = other.handle;
		return *this;
	}
	~RootSocket() {}

	// Check if the handle is valid
	explicit operator bool() const
	{
		return handle != INVALID_HANDLE;
	}

	// Send a message through the root socket, optionally with a some IPC handles.
	// Returns the number of bytes sent or -1 on error
	bool SendMsg(const void* data, size_t len, IPCHandle* const* handles, size_t num_handles) const;

	// Recieve a message from the root socket, will block until a message arrives.
	// Up to num_handles handles are retrieved from the message, any further handles are discarded.
	// If there are less than num_handles handles, the remaining entries are filled with NULL pointers.
	// Returns the number of bytes recieved or -1 on error
	bool RecvMsg(std::vector<char>& buffer, std::vector<IPCHandle>& handles) const;

	// Overloads for sending and recieving data without any handles
	inline bool SendMsg(const void* data, size_t len) const
	{
		return SendMsg(data, len, NULL, 0);
	}
	inline bool RecvMsg(std::vector<char>& buffer) const
	{
		std::vector<IPCHandle> handles;
		return RecvMsg(buffer, handles);
	}

private:
	OSHandleType handle;

	friend RootSocket GetRootSocket();
	friend class Module;
};

// Host-only definitions

class Module {
public:
	Module()
		: process_handle(INVALID_HANDLE) {}
	~Module()
	{
		Close();
	}
	Module(Module&& other) NACL_NOEXCEPT
	{
		process_handle = other.process_handle;
		root_socket = other.root_socket;
		other.process_handle = INVALID_HANDLE;
	}
	Module& operator=(Module&& other) NACL_NOEXCEPT
	{
		std::swap(process_handle, other.process_handle);
		std::swap(root_socket, other.root_socket);
		return *this;
	}

	// Check if the handle is valid
	explicit operator bool() const
	{
		return process_handle != INVALID_HANDLE;
	}

	// Close the module and kill the NaCl process
	void Close();

	// Get the root socket of this module
	RootSocket GetRootSocket() const
	{
		RootSocket out;
		out.handle = root_socket;
		return out;
	}

private:
	OSHandleType process_handle;
	OSHandleType root_socket;

	friend Module InternalLoadModule(OSHandleType*, const char* const*, const char* const*, bool);
};

// NaCl module loader paramters
struct LoaderParams {
	// Secure ELF loader executable
	const char* sel_ldr;

	// Integrated runtime
	const char* irt;

	// Bootstrap program, only used on Linux
	const char* bootstrap;
};

// Load a module. If params is null, a native client executable is loaded,
// otherwise a host executable is loaded. If use_debugger is enabled, a
// gdbserver instance will be started on localhost:4014.
Module LoadModule(const char* module, const LoaderParams* nacl_params, bool use_debugger);

// Module-only definitions
// Create the root socket for the current module.
RootSocket GetRootSocket();

} // namespace NaCl

#endif // NACL_H_
