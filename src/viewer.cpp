#include <stdio.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0x00;

void render_frame(){
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

}

void initialize_graphics(){
    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.majorVersion = 2;
    ctx = emscripten_webgl_create_context("#viewport", &attr);
    emscripten_webgl_make_context_current(ctx);
    emscripten_set_main_loop(render_frame, 0, 1);
}



int main(int argc, char** argv){
    initialize_graphics();
    return 0;
}