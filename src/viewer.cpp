#include <stdio.h>
#include <math.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0x00;

GLuint shader_program = 0x00;

GLuint vao = 0;
GLuint vbo = 0;

GLint u_mvp_loc = -1;

float cam_theta = 0.0f;
float cam_phi = 0.3f;
float cam_dist = 3.0f;


bool dragging = false;
int last_x = 0, last_y=0;


float triangle_vertices[] = {

    0.0f, 0.5f, 0.0f, //top

    -0.5f, -0.5f, 0.0f, // bottom left

    0.5f, -0.5f, 0.0f, //bottom right

};


//Shader information and positions
const char* vertex_shader_source = R"glsl(#version 300 es
    precision highp float;
    layout(location = 0 ) in vec3 a_position;
    uniform mat4 u_mvp;
    void main(){
        gl_Position = u_mvp*vec4(a_position, 1.0);
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



void mat4_look_at(float* out, float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z, float up_x, float up_y, float up_z){
    

    //Distance
    float fx = target_x - eye_x;
    float fy = target_y - eye_y;
    float fz = target_z - eye_z;

    float f_len = sqrtf(fx*fx + fy*fy + fz*fz);

    //Right Axis
    float rx = fy*up_z - fz*up_y;
    float ry = fz*up_x - fx*up_z;
    float rz = fx*up_y - fy*up_x;

    float r_len = sqrtf(rx*rx + ry*ry + rz*rz);

    rx /= r_len; ry /= r_len; rz /= r_len;

    //True Up (The actual Y vector)
    // True up: right × forward
    float ux = ry*fz - rz*fy;
    float uy = rz*fx - rx*fz;
    float uz = rx*fy - ry*fx;

    // Column-major 4x4
    out[0]=rx;  out[4]=ry;  out[8] =rz;  out[12] = -(rx*eye_x + ry*eye_y + rz*eye_z);
    out[1]=ux;  out[5]=uy;  out[9] =uz;  out[13] = -(ux*eye_x + uy*eye_y + uz*eye_z);
    out[2]=-fx; out[6]=-fy; out[10]=-fz; out[14] =  (fx*eye_x + fy*eye_y + fz*eye_z);
    out[3]=0;   out[7]=0;   out[11]=0;   out[15] = 1.0f;

}



void mat4_perspective(float* out, float fov_rad, float aspect, float near, float far) {
    float f = 1.0f / tanf(fov_rad * 0.5f);
    float nf = 1.0f / (near - far);

    out[0] = f / aspect;
    out[1] = 0; out[2] = 0; out[3] = 0;

    out[4] = 0;
    out[5] = f;
    out[6] = 0; out[7] = 0;

    out[8] = 0; out[9] = 0;
    out[10] = (far + near) * nf;
    out[11] = -1.0f;

    out[12] = 0; out[13] = 0;
    out[14] = 2.0f * far * near * nf;
    out[15] = 0;
}

void mat4_multiply(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            tmp[c*4 + r] = a[0*4 + r]*b[c*4 + 0] +
                           a[1*4 + r]*b[c*4 + 1] +
                           a[2*4 + r]*b[c*4 + 2] +
                           a[3*4 + r]*b[c*4 + 3];
    for (int i = 0; i < 16; i++) out[i] = tmp[i];
}

//Render frames and refresh canvas to be color (glClearColor)
void render_frame(){
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shader_program);

    // Inside render_frame, replacing the previous view-only block:

    float eye_x = cam_dist * cosf(cam_phi) * sinf(cam_theta);
    float eye_y = cam_dist * sinf(cam_phi);
    float eye_z = cam_dist * cosf(cam_phi) * cosf(cam_theta);

    int w, h;
    emscripten_get_canvas_element_size("#viewport", &w, &h);
    float aspect = (float)w / (float)h;

    float view[16], proj[16], mvp[16];
    mat4_look_at(view, eye_x, eye_y, eye_z,
                       0.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f);
    mat4_perspective(proj, 1.0472f, aspect, 0.1f, 100.0f);  // 1.0472 ≈ 60° in radians
    mat4_multiply(mvp, proj, view);

    glUniformMatrix4fv(u_mvp_loc, 1, GL_FALSE, mvp);


    
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

}

EM_BOOL on_mousedown(int type, const EmscriptenMouseEvent* e, void* ud){
    if (e->button == 0){
        dragging = true;
        last_x = e->clientX;
        last_y = e->clientY;
    }

    return EM_TRUE;
}

EM_BOOL on_mouseup(int type, const EmscriptenMouseEvent* e, void* ud){
    if(e->button == 0){
        dragging = false;
    }

    return EM_TRUE;

}

EM_BOOL on_mousemove(int type, const EmscriptenMouseEvent* e, void* ud){
    if(!dragging) return EM_TRUE;

    int dx = e->clientX - last_x;
    int dy = e->clientY - last_y;

    cam_theta -= dx*0.01f;
    cam_phi += dy*0.01f;

    if(cam_phi > 1.5f) cam_phi = 1.5f;
    if(cam_phi < -1.5f) cam_phi = -1.5f;

    last_x = e->clientX;
    last_y = e->clientY;
    return EM_TRUE;
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

    u_mvp_loc = glGetUniformLocation(shader_program, "u_mvp");

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle_vertices), triangle_vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    emscripten_set_mousedown_callback("#viewport", nullptr, EM_TRUE, on_mousedown);
    emscripten_set_mouseup_callback("#viewport",   nullptr, EM_TRUE, on_mouseup);
    emscripten_set_mousemove_callback("#viewport", nullptr, EM_TRUE, on_mousemove);


    glDeleteShader(vs);
    glDeleteShader(fs);

    emscripten_set_main_loop(render_frame, 0, 1);
}



int main(int argc, char** argv){

    initialize_graphics();
    return 0;
}