#include <stdio.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0x00;

GLuint shader_program = 0x00;

GLuint vao = 0;
GLuint vbo = 0;

float triangle_vertices[] = {

    0.0f, 0.5f, 0.0f, //top

    -0.5f, -0.5f, 0.0f, // bottom left

    0.5f, -0.5f, 0.0f, //bottom right

};


//Shader information and positions
const char* vertex_shader_source = R"glsl(#version 300 es
    precision highp float;
    layout(location = 0 ) in vec3 a_position;
    void main(){
        gl_Position = vec4(a_position, 1.0);
    }
)glsl";

const char* fragment_shader_source = R"glsl(#version 300 es
    precision highp float;
    out vec4 frag_color;
    void main(){
        frag_color = vec4(0.29, 0.87, 0.50, 1.0);
    }
)glsl";


GLuint link_program(GLuint vertex_shader, GLuint fragment_shader){
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);

    glLinkProgram(program);

    return program;
}


//Compute shaders to render on ctx
GLuint compile_shader(GLenum type, const char* source){
    GLuint shader_ID = glCreateShader(type);
    glShaderSource(shader_ID, 1, &source, nullptr);
    glCompileShader(shader_ID);
    return shader_ID;
}

//Render frames and refresh canvas to be color (glClearColor)
void render_frame(){
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shader_program);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

}

//Initilize graphics and setup context for the WebGL
void initialize_graphics(){

    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.majorVersion = 2;
    ctx = emscripten_webgl_create_context("#viewport", &attr);
    emscripten_webgl_make_context_current(ctx);

    
    double css_width, css_height;
    emscripten_get_element_css_size("#viewport", &css_width, &css_height);

    double dpr = emscripten_get_device_pixel_ratio();

    int buffer_width = (int)(css_width*dpr);
    int buffer_height = (int)(css_height*dpr);

    emscripten_set_canvas_element_size("#viewport", buffer_width, buffer_height);
    glViewport(0, 0, buffer_width, buffer_height);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);

    shader_program = link_program(vs,fs);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle_vertices), triangle_vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);


    glDeleteShader(vs);
    glDeleteShader(fs);

    emscripten_set_main_loop(render_frame, 0, 1);
}



int main(int argc, char** argv){

    initialize_graphics();
    return 0;
}