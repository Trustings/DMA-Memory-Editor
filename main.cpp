#include "memory.hpp"
#include "list.hpp"
#include "render.hpp"
#include "core.hpp"

void Render_begin(){
//This function is redundant.
}

int main()
{
    pthread_t thread_id;

    pthread_create(&thread_id, NULL, sync_operations, NULL);

    Initialize();

    /*
    while (true) {
        printf("testing\n");
\
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Prints every second
    }
    */

    if (!(Render::InitVulkan()))
    {
        printf("Failed to init Vulkan!\n");
        system("pause");

    }

    Render::RenderLoop(Render_begin);

    pthread_join(thread_id, NULL);

    return 0;
}
