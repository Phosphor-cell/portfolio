#include <stdio.h>
#include <math.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <stdlib.h>
#include <string.h>


EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0x00;

GLuint shader_program = 0x00;

GLuint vao = 0;
GLuint vbo = 0;

GLint u_mvp_loc = -1;

GLint u_cam_pos_loc = -1;


GLuint grid_program = 0;
GLuint grid_vao = 0;
GLint grid_u_inv_vp_loc = -1;
GLint grid_u_cam_pos_loc = -1;
GLint grid_u_mvp_loc = -1;

GLint grid_u_vp_loc = -1;  // new global

float* mesh_positions = nullptr;
float* mesh_normals = nullptr;
unsigned int* mesh_indices = nullptr;
int mesh_vertex_count = 0;
int mesh_index_count = 0;
GLuint ebo = 0;
GLuint nbo = 0;



float cam_theta = 0.0f;
float cam_phi = 0.25f;
float cam_dist = 5.0f;


bool dragging = false;
int last_x = 0, last_y=0;


float triangle_vertices[] = {

    0.0f, 0.5f, 0.0f, //top

    -0.5f, -0.5f, 0.0f, // bottom left

    0.5f, -0.5f, 0.0f, //bottom right

};

const char* grid_vertex_shader_source = R"glsl(#version 300 es
    precision highp float;
    out vec2 v_clip;

    void main() {
        vec2 positions[6] = vec2[](
            vec2(-1.0, -1.0),
            vec2( 1.0, -1.0),
            vec2( 1.0,  1.0),
            vec2(-1.0, -1.0),
            vec2( 1.0,  1.0),
            vec2(-1.0,  1.0)
        );
        v_clip = positions[gl_VertexID];
        gl_Position = vec4(v_clip, 0.0, 1.0);
    }
)glsl";


const char* grid_fragment_shader_source = R"glsl(#version 300 es
    #extension GL_EXT_frag_depth : enable

    precision highp float;

    in vec2 v_clip;
    uniform mat4 u_inv_vp;
    uniform mat4 u_vp;
    uniform vec3 u_cam_pos;

    out vec4 frag_color;

    void main() {
        vec4 world_far = u_inv_vp * vec4(v_clip, 1.0, 1.0);
        world_far.xyz /= world_far.w;
        vec3 ray_dir = normalize(world_far.xyz - u_cam_pos);

        float plane_y = -1.0;
        float t = (plane_y - u_cam_pos.y) / ray_dir.y;
        if (t < 0.0) discard;

        vec3 hit = u_cam_pos + t * ray_dir;

        vec2 grid_coord = hit.xz;
        vec2 deriv = fwidth(grid_coord);
        vec2 f = fract(grid_coord);
        vec2 dist_to_line = min(f, 1.0 - f) / deriv;
        float line_dist = min(dist_to_line.x, dist_to_line.y);
        float line = 1.0 - smoothstep(0.0, 1.5, line_dist);

        // Distance-based fade: fully visible up to fade_start, gone by fade_end
        float dist_from_cam = length(hit - u_cam_pos);
        float fade_start = 15.0;
        float fade_end   = 30.0;
        float fade = 1.0 - smoothstep(fade_start, fade_end, dist_from_cam);

        float alpha = line * fade;
        if (alpha < 0.01) discard;

        // Correct depth so the sphere occludes the grid properly
        vec4 clip_pos = u_vp * vec4(hit, 1.0);
        gl_FragDepth = (clip_pos.z / clip_pos.w) * 0.5 + 0.5;

        vec3 line_color = vec3(0.3, 0.1, 0.50);
        frag_color = vec4(line_color, alpha);
    }
)glsl";
//Shader information and positions
const char* vertex_shader_source = R"glsl(#version 300 es
    precision highp float;
    layout(location = 0 ) in vec3 a_position;
    layout(location = 1) in vec3 a_normal;
    uniform mat4 u_mvp;
    out vec3 v_world_pos;
    out vec3 v_normal;
    void main(){
        v_world_pos = a_position;
        v_normal = a_normal;
        gl_Position = u_mvp*vec4(a_position, 1.0);
    }
)glsl";

const char* fragment_shader_source = R"glsl(#version 300 es
    precision highp float;
    in vec3 v_world_pos;
    in vec3 v_normal;
    uniform vec3 u_cam_pos;
    out vec4 frag_color;

    // Damascus wave pattern — layered sines with domain warp
    float damascus(vec3 p) {
        // Domain warp: shifts sampling position by another wave field,
        // which turns straight bands into flowing curves
        vec3 warp = vec3(
            sin(p.y * 1.8 + p.z * 0.9),
            sin(p.z * 1.6 + p.x * 1.1),
            sin(p.x * 1.4 + p.y * 1.3)
        ) * 0.8;
        vec3 q = p + warp;

        // Primary directional bands
        float d = sin(q.x * 3.5 + q.y * 1.2 + q.z * 0.8);
        // Finer detail
        d += 0.5 * sin(q.y * 7.0 + q.x * 2.0);
        d += 0.25 * sin(q.z * 11.0 - q.x * 3.5);
        d += 0.12 * sin(q.x * 19.0 + q.z * 8.0);

        return d;
    }

    void main(){
        vec3 N = normalize(v_normal);
        vec3 V = normalize(u_cam_pos - v_world_pos);
        vec3 L = normalize(vec3(0.6, 0.8, 0.4));
        vec3 H = normalize(L + V);

        float NdotV = max(dot(N, V), 0.0);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);

        // Sample damascus in world space — no seams, 3D-consistent
        float d = damascus(v_world_pos * 2.2);
        // Normalize to [0, 1] and sharpen the band contrast
        float band = smoothstep(-0.3, 0.3, d);

        // Two-tone steel palette
        vec3 dark_steel  = vec3(0.18, 0.20, 0.24);   // oxidized layer
        vec3 light_steel = vec3(0.78, 0.80, 0.85);   // polished layer
        vec3 base = mix(dark_steel, light_steel, band);

        // Thin bright etch lines at band boundaries — the signature damascus vein
        float edge = 1.0 - smoothstep(0.0, 0.1, abs(d));
        base += vec3(1.0, 0.95, 0.85) * edge * 0.4;

        // Metallic shading — strong diffuse falloff
        float shadow = 0.25 + 0.75 * NdotL;
        vec3 color = base * shadow;

        // Anisotropic-ish specular — metals reflect sharply
        float spec = pow(NdotH, 100.0);
        // Specular tints with the underlying steel band
        color += mix(vec3(0.6, 0.55, 0.5), vec3(1.0, 0.95, 0.85), band) * spec * 1.8;

        // Subtle silhouette
        float fresnel = pow(1.0 - NdotV, 4.0);
        color += vec3(0.5, 0.55, 0.65) * fresnel * 0.3;

        frag_color = vec4(color, 1.0);
    }
)glsl";

GLuint link_program(GLuint vertex_shader, GLuint fragment_shader){
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);

    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        printf("PROGRAM LINK ERROR:\n%s\n", log);
    }

    return program;
}


//Compute shaders to render on ctx
GLuint compile_shader(GLenum type, const char* source){
    GLuint shader_ID = glCreateShader(type);
    glShaderSource(shader_ID, 1, &source, nullptr);
    glCompileShader(shader_ID);

    GLint ok = 0;
    glGetShaderiv(shader_ID, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader_ID, sizeof(log), nullptr, log);
        printf("SHADER COMPILE ERROR:\n%s\n", log);
    }
    return shader_ID;
}



void mat4_look_at(float* out, float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z, float up_x, float up_y, float up_z){
    

    //Distance
    float fx = target_x - eye_x;
    float fy = target_y - eye_y;
    float fz = target_z - eye_z;

    float f_len = sqrtf(fx*fx + fy*fy + fz*fz);
    fx /= f_len; fy /= f_len; fz /= f_len;  


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


void mat4_inverse_view(float* out, float eye_x, float eye_y, float eye_z, float target_x, float target_y, float target_z, float up_x, float up_y, float up_z){
     //Distance
    float fx = target_x - eye_x;
    float fy = target_y - eye_y;
    float fz = target_z - eye_z;

    float f_len = sqrtf(fx*fx + fy*fy + fz*fz);
    fx /= f_len; fy /= f_len; fz /= f_len;  


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
    
    out[0] = rx;   out[4] = ux;   out[8]  = -fx;  out[12] = eye_x;
    out[1] = ry;   out[5] = uy;   out[9]  = -fy;  out[13] = eye_y;
    out[2] = rz;   out[6] = uz;   out[10] = -fz;  out[14] = eye_z;
    out[3] = 0.0f; out[7] = 0.0f; out[11] = 0.0f; out[15] = 1.0f;

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

void mat4_inverse_perspective(float* out, float fov_rad, float aspect, float near, float far) {
    float f = 1.0f / tanf(fov_rad * 0.5f);

    out[0] = aspect / f;  out[1] = 0;      out[2] = 0;   out[3]  = 0;
    out[4] = 0;           out[5] = 1.0f/f; out[6] = 0;   out[7]  = 0;
    out[8] = 0;           out[9] = 0;      out[10] = 0;  out[11] = (near - far) / (2.0f * far * near);
    out[12]= 0;           out[13]= 0;      out[14]= -1;  out[15] = (far + near) / (2.0f * far * near);
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
    // --- Keep canvas drawing buffer in sync with CSS size ---
    double css_w, css_h;
    emscripten_get_element_css_size("#viewport", &css_w, &css_h);
    double dpr = emscripten_get_device_pixel_ratio();
    int buf_w = (int)(css_w * dpr);
    int buf_h = (int)(css_h * dpr);

    int cur_w, cur_h;
    emscripten_get_canvas_element_size("#viewport", &cur_w, &cur_h);
    if (cur_w != buf_w || cur_h != buf_h) {
        emscripten_set_canvas_element_size("#viewport", buf_w, buf_h);
    }

    glViewport(0, 0, buf_w, buf_h);
    float aspect = (float)buf_w / (float)buf_h;
    printf("buf=%dx%d aspect=%.3f\n", buf_w, buf_h, aspect);

    // --- Clear and draw ---
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader_program);

    double now = emscripten_get_now();     // ms since page load
    static double last = now;
    float dt = (float)(now - last) * 0.001f;
    last = now;
    
    if (!dragging) {
        cam_theta += 0.15f * dt;  // 0.5 radians per second, framerate-independent
    }
    

    float eye_x = cam_dist * cosf(cam_phi) * sinf(cam_theta);
    float eye_y = cam_dist * sinf(cam_phi);
    float eye_z = cam_dist * cosf(cam_phi) * cosf(cam_theta);
    
    float view[16], proj[16], mvp[16];
    mat4_look_at(view, eye_x, eye_y, eye_z,
                       0.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f);
    mat4_perspective(proj, 1.0472f, aspect, 0.1f, 100.0f);
    mat4_multiply(mvp, proj, view);

    float inv_view[16], inv_proj[16], inv_vp[16];
    mat4_inverse_view(inv_view, eye_x, eye_y, eye_z, 0, 0, 0, 0, 1, 0);
    mat4_inverse_perspective(inv_proj, 1.0472f, aspect, 0.1f, 100.0f);
    mat4_multiply(inv_vp, inv_view, inv_proj);  // inv(V*P)... wait — see below

    glUniformMatrix4fv(u_mvp_loc, 1, GL_FALSE, mvp);
    glUniform3f(u_cam_pos_loc, eye_x, eye_y, eye_z);

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, mesh_index_count, GL_UNSIGNED_INT, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    

    glUseProgram(grid_program);
    glUniformMatrix4fv(grid_u_inv_vp_loc, 1, GL_FALSE, inv_vp);
    glUniformMatrix4fv(grid_u_vp_loc, 1, GL_FALSE, mvp);
    glUniform3f(grid_u_cam_pos_loc, eye_x, eye_y, eye_z);
    glBindVertexArray(grid_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);


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

EM_BOOL on_wheel(int type, const EmscriptenWheelEvent* e, void* ud){
    cam_dist *= 1.0f + (float)e->deltaY * 0.001f;

    if(cam_dist < 0.1f) cam_dist = 0.1f;
    if(cam_dist > 50.0f) cam_dist = 50.0f;

    return EM_TRUE;
}


void compute_smooth_normals(){
    mesh_normals = (float*)calloc(mesh_vertex_count * 3, sizeof(float));

    for (int i = 0; i < mesh_index_count; i += 3) {
        unsigned int i0 = mesh_indices[i];
        unsigned int i1 = mesh_indices[i+1];
        unsigned int i2 = mesh_indices[i+2];

        float* p0 = &mesh_positions[i0*3];
        float* p1 = &mesh_positions[i1*3];
        float* p2 = &mesh_positions[i2*3];

        //The edges of the tringle
        float e1x = p1[0]-p0[0], e1y = p1[1]-p0[1], e1z = p1[2]-p0[2];
        float e2x = p2[0]-p0[0], e2y = p2[1]-p0[1], e2z = p2[2]-p0[2];

        //face normals = e1 x e2
        float nx = e1y*e2z - e1z*e2y;
        float ny = e1z*e2x - e1x*e2z;
        float nz = e1x*e2y - e1y*e2x;

        mesh_normals[i0*3+0] += nx; mesh_normals[i0*3+1] += ny; mesh_normals[i0*3+2] += nz;
        mesh_normals[i1*3+0] += nx; mesh_normals[i1*3+1] += ny; mesh_normals[i1*3+2] += nz;
        mesh_normals[i2*3+0] += nx; mesh_normals[i2*3+1] += ny; mesh_normals[i2*3+2] += nz;

    }

     // Normalize each vertex normal to unit length
    for (int i = 0; i < mesh_vertex_count; i++) {
        float* n = &mesh_normals[i*3];
        float len = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (len > 1e-8f) { n[0]/=len; n[1]/=len; n[2]/=len; }
    }
}

void normalize_mesh_bounds() {
    if (mesh_vertex_count == 0) return;

    // Find bounding box
    float min_x = mesh_positions[0], max_x = min_x;
    float min_y = mesh_positions[1], max_y = min_y;
    float min_z = mesh_positions[2], max_z = min_z;

    for (int i = 1; i < mesh_vertex_count; i++) {
        float x = mesh_positions[i*3+0];
        float y = mesh_positions[i*3+1];
        float z = mesh_positions[i*3+2];
        if (x < min_x) min_x = x; if (x > max_x) max_x = x;
        if (y < min_y) min_y = y; if (y > max_y) max_y = y;
        if (z < min_z) min_z = z; if (z > max_z) max_z = z;
    }

    // Center and find largest extent
    float cx = (min_x + max_x) * 0.5f;
    float cy = (min_y + max_y) * 0.5f;
    float cz = (min_z + max_z) * 0.5f;
    float ex = max_x - min_x;
    float ey = max_y - min_y;
    float ez = max_z - min_z;
    float largest = ex;
    if (ey > largest) largest = ey;
    if (ez > largest) largest = ez;
    float scale = 2.0f / largest;  // fit into a [-1, 1] cube

    // Recenter + uniformly scale
    for (int i = 0; i < mesh_vertex_count; i++) {
        mesh_positions[i*3+0] = (mesh_positions[i*3+0] - cx) * scale;
        mesh_positions[i*3+1] = (mesh_positions[i*3+1] - cy) * scale;
        mesh_positions[i*3+2] = (mesh_positions[i*3+2] - cz) * scale;
    }

    printf("Normalized: extents were (%.2f, %.2f, %.2f), scale=%.3f\n", ex, ey, ez, scale);
}

// Parse a minimal OBJ file (v and f lines only)
bool load_obj(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { printf("Cannot open %s\n", path); return false; }

    // --- Pass 1: count ---
    int v_count = 0, tri_count = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='v' && line[1]==' ') v_count++;
        else if (line[0]=='f' && line[1]==' ') {
            // Count spaces to figure out whether it's a triangle or a quad
            int verts_on_line = 0;
            for (char* p = line + 1; *p; p++) {
                if (*p == ' ' && *(p+1) != '\0' && *(p+1) != '\n' && *(p+1) != ' ')
                    verts_on_line++;
            }
            if (verts_on_line == 3) tri_count += 1;
            else if (verts_on_line == 4) tri_count += 2;  // quad → 2 triangles
        }
    }

    // --- Free any previous mesh ---
    free(mesh_positions); free(mesh_normals); free(mesh_indices);
    mesh_positions = (float*)malloc(v_count * 3 * sizeof(float));
    mesh_indices = (unsigned int*)malloc(tri_count * 3 * sizeof(unsigned int));
    mesh_index_count = tri_count * 3;
    mesh_vertex_count = v_count;

    // --- Pass 2: fill ---
    rewind(f);
    int vi = 0, fi = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='v' && line[1]==' ') {
            sscanf(line, "v %f %f %f",
                   &mesh_positions[vi*3+0],
                   &mesh_positions[vi*3+1],
                   &mesh_positions[vi*3+2]);
            vi++;
        } else if (line[0]=='f' && line[1]==' ') {
            int a=0, b=0, c=0, d=0;
            int dummy;
            int n = sscanf(line, "f %d/%d/%d %d/%d/%d %d/%d/%d %d/%d/%d",
                           &a,&dummy,&dummy, &b,&dummy,&dummy,
                           &c,&dummy,&dummy, &d,&dummy,&dummy);
            int verts = n / 3;
            
            if (verts < 3) {
                // Try "f a//n b//n c//n d//n"
                n = sscanf(line, "f %d//%d %d//%d %d//%d %d//%d",
                           &a,&dummy, &b,&dummy, &c,&dummy, &d,&dummy);
                verts = n / 2;
            }
            if (verts < 3) {
                // Try plain "f a b c d"
                n = sscanf(line, "f %d %d %d %d", &a, &b, &c, &d);
                verts = n;
            }
        
            mesh_indices[fi*3+0] = a - 1;
            mesh_indices[fi*3+1] = b - 1;
            mesh_indices[fi*3+2] = c - 1;
            fi++;
        
            if (verts == 4) {
                mesh_indices[fi*3+0] = a - 1;
                mesh_indices[fi*3+1] = c - 1;
                mesh_indices[fi*3+2] = d - 1;
                fi++;
            }
        }
    }
    fclose(f);

    compute_smooth_normals();
    normalize_mesh_bounds();
    printf("Loaded %s: %d verts, %d tris\n", path, v_count, tri_count);
    return true;
}

//Initilize graphics and setup context for the WebGL
void initialize_graphics(){

    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.antialias = EM_TRUE;

    attr.majorVersion = 2;
    ctx = emscripten_webgl_create_context("#viewport", &attr);
    emscripten_webgl_make_context_current(ctx);
    glEnable(GL_DEPTH_TEST);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);

    shader_program = link_program(vs,fs);

    u_mvp_loc = glGetUniformLocation(shader_program, "u_mvp");
    u_cam_pos_loc = glGetUniformLocation(shader_program, "u_cam_pos");

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    //glGenBuffers(1, &vbo);
    //glBindBuffer(GL_ARRAY_BUFFER, vbo);
    //glBufferData(GL_ARRAY_BUFFER, sizeof(triangle_vertices), triangle_vertices, GL_STATIC_DRAW);
    //glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    //glEnableVertexAttribArray(0);
    if(!load_obj("meshes/sphere.obj")) return;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh_vertex_count*3*sizeof(float), mesh_positions, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glGenBuffers(1, &nbo);
    glBindBuffer(GL_ARRAY_BUFFER, nbo);
    glBufferData(GL_ARRAY_BUFFER, mesh_vertex_count * 3 * sizeof(float), mesh_normals, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);

    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh_index_count*sizeof(unsigned int), mesh_indices, GL_STATIC_DRAW);

    glGenVertexArrays(1, &grid_vao);
    GLuint gvs = compile_shader(GL_VERTEX_SHADER, grid_vertex_shader_source);
    GLuint gfs = compile_shader(GL_FRAGMENT_SHADER, grid_fragment_shader_source);

    grid_program = link_program(gvs,gfs);

    grid_u_mvp_loc = glGetUniformLocation(grid_program, "u_mvp");

    grid_u_inv_vp_loc  = glGetUniformLocation(grid_program, "u_inv_vp");
    grid_u_vp_loc = glGetUniformLocation(grid_program, "u_vp");
    grid_u_cam_pos_loc = glGetUniformLocation(grid_program, "u_cam_pos");





    emscripten_set_mousedown_callback("#viewport", nullptr, EM_TRUE, on_mousedown);
    emscripten_set_mouseup_callback("#viewport",   nullptr, EM_TRUE, on_mouseup);
    emscripten_set_mousemove_callback("#viewport", nullptr, EM_TRUE, on_mousemove);
    emscripten_set_wheel_callback("#viewport", nullptr, EM_TRUE, on_wheel);


    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteShader(gvs);
    glDeleteShader(gfs);

    emscripten_set_main_loop(render_frame, 0, 1);
}



int main(int argc, char** argv){

    initialize_graphics();
    return 0;
}