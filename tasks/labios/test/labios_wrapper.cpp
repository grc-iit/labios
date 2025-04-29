#include "labios/labios_client.h"
#include <cstring>

extern "C" {

void* labios_create_client() {
    CHIMAERA_CLIENT_INIT();
    auto* client = new chi::labios::Client();
    client->Create(
        HSHM_MCTX,
        chi::DomainQuery::GetDirectHash(chi::SubDomainId::kGlobalContainers, 0),
        chi::DomainQuery::GetGlobalBcast(),
        "ipc_test"
    );
    return static_cast<void*>(client);
}

void labios_write(void* client_ptr, const char* key, const char* data) {
    auto* client = static_cast<chi::labios::Client*>(client_ptr);
    size_t size = std::strlen(data) + 1;

    hipc::FullPtr<char> buffer = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, size);
    std::memcpy(buffer.ptr_, data, size);

    client->Write(HSHM_MCTX,
                  chi::DomainQuery::GetLocalHash(0),
                  key,
                  buffer.shm_,
                  size);
}

char* labios_read(void* client_ptr, const char* key) {
    auto* client = static_cast<chi::labios::Client*>(client_ptr);
    size_t size = 1024;  // should match size used in write

    hipc::FullPtr<char> buffer = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, size);
    client->Read(HSHM_MCTX,
                 chi::DomainQuery::GetLocalHash(0),
                 key,
                 buffer.shm_,
                 size);

    return buffer.ptr_;  // make sure Python reads it as a C string
}
}
