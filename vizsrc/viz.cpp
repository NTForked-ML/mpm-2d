#include <iostream>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#include <SDL/SDL_image.h>
#include <png.h>
#include <FTGL/ftgl.h>

#include <getopt.h>
#include <unistd.h>

#include <assert.h>

#include <confuse.h>

#include "viz_colormap.hpp"
#include "viz_builtin_colormap.hpp"

#include "viz_particle.hpp"
#include "viz_element.hpp"
#include "viz_reader.hpp"

using namespace FTGL;

#define CROSS_SIZE 0.05f
#define PARTICLE_SIZE 5.0f

#define VAR_DISPLACEMENT 0
#define VAR_VELOCITY 1
#define VAR_PRESSURE 5

#define RESTRICT_VALUE(var,lower,upper) \
do { \
    if ((var) > (upper)) { \
        var = (upper); \
    } else if ((var) < (lower)) { \
        var = (lower); \
    } \
} while (0)

template <typename fp, size_t dims> 
class Point
{
    private:
        std::array<fp, dims> coordinates;

    public:
        Point(fp x = 0, fp y = 0, fp z = 0)
        {
            setCoordinate(0, x);
            setCoordinate(1, y);
            setCoordinate(2, z);
            return;
        }
        void setCoordinate(size_t dim, fp value)
        {
            if (dim < dims) {
                coordinates[dim] = value;
            }
            return;
        }

        fp getCoordinate(size_t dim)
        {
            fp r;
            if (dim < dims) {
                r = coordinates[dim];
            }
            return r;
        }
};

typedef Point<float, 2> Point2f;

template <size_t dims>
class Renderer
{
    public:
        virtual ~Renderer();
        virtual void setColor(rgbaColor &color) = 0;
//        virtual void drawQuad(Point &p0, Point &p1, Point &p2, Point &p3);
        virtual void drawQuad(Point<double, dims> *begin, Point<double, dims> *end);
        virtual void drawQuadstrip(Point<double, dims> *begin, Point<double, dims> *end);

//        template <typename Iterator>
//        virtual void drawParticles(Iterator pBegin, Iterator pEnd);
};

template <size_t dims>
class OpenGLRenderer : public Renderer<dims>
{
    public:
        void setColor(rgbaColor &color)
        {
            glColor4f(color.getr(), color.getg(), color.getg(), color.geta());
        }

        void drawQuad(Point<double, dims> *begin, Point<double, dims> *end)
        {
            size_t numPoints = (end - begin);

            assert(numPoints == 4);

            return;
        }

        void drawQuadstrip(Point<double, dims> *begin, Point<double, dims> *end)
        {

//            glBegin(GL_QUADS);
//            for (i = 0; i < gradation; i++) {
//                apply_colormap(cm, i * delta, &r, &g, &b);
//                glColor3f(r, g, b);
//                xq0 = xl;
//                yq0 = yl + h*i*delta;
//                glVertex3f(xq0, yq0, -1.0f);
//                glVertex3f(xq0 + w, yq0, -1.0f);
//                glVertex3f(xq0 + w, yq0 + delta*h, -1.0f);
//                glVertex3f(xq0, yq0 + delta*h, -1.0f);
//            }
//            glEnd();
            return;
        }
};

void apply_colormap(cm_t *cm, float val, float *r, float *g, float *b);

static struct g_state_s {
    FILE *data_file;        /* which data file */
    int is_element_file;    /* -e option: particle or element data file? */
    double data_min;        /* -l option: lower bound in graph */
    double data_max;        /* -u option: upper bound in graph */
    int data_autoscale;     /* -a option: autoscale data to min,max */
    int data_var;           /* -p option: which variable to plot */
    int draw_stress_tensor; /* -s option: draw cross representing stress tensor */
    int write_frames;       /* -w option: write to files in figs/viz dir */
    double principal_stress_max;
    double principal_stress_min;
    double principal_stress_delta;

    double abs_ps_min;
    double abs_ps_max;
    double abs_ps_delta;

    double wanted_fps;

    double *scene_lines;    /* -c option: scene file */
    int num_scene_lines;
    int mirror_x;           /* mirror across x axis */
    int mirror_y;           /* mirror across y axis */

    cm_t colormap;

    rgba_t bgcolor;

    int paused;
    int step_next;

    double camera_view[3];  /* has defaults, but also set in scene file. */

    int draw_velocity_vector;

    float particle_size;

    cfg_t *cfg; /* configuration file */
    cfg_opt_t *opts;
    int color_override;
    int draw_glyphs;

    int want_colorbar;
    int logscale;

    char wm_title[1024];
    char loaded_file_path[1024];

    Colormap *cmap;
    SimulationReader *reader;
} g_state;

typedef struct drawing_object_s {
    size_t num_verticies;
    GLenum mode;
    GLfloat *vertex_coords;
    GLfloat *color;
} drawing_object_t;

static const char* g_optstring = "ab:c:d:m:el:u:p:svwCL";

int screen_width = 1000;
int screen_height = 800;
const int screen_bpp = 32;

double current_time;
int current_frame;

double current_fps;

SDL_Surface *screen = NULL; // create a default sdl_surface to render our opengl to
FTGLfont *font = NULL;
FTGLfont *small_font = NULL;
FTGLlayout *centered_layout = NULL;
FTGLlayout *small_centered_layout = NULL;
FTGLlayout *small_left_layout = NULL;
char pngbase[] = "figs/viz/frame";
char pngfile[1024];

const int bgsize = 101;
int which_element(double x, double y, int N)
{
    double h = 1.0 / (N-1);
    int r = floor(y / h);
    int c = floor(x / h);
    return (N-1) * r + c;
}

//assume list has length at least 4
void element_to_node_list(int *list, int elem, int N)
{
    int i = elem % (N - 1);
    int j = elem / (N - 1);
    
    list[0] = j*(N) + i;
    list[1] = j*(N) + i + 1;
    list[2] = (j + 1)*(N) + i + 1;
    list[3] = (j + 1)*(N) + i;

    return;
}

typedef struct s_aux_particle {
    double x;
    double y;

    double x_t;
    double y_t;

    double sxx;
    double sxy;
    double syy;

    double ux;
    double uy;

    double m;
    double v;
    double gammap;
    double color;
    double magEf;

    double active;

    double s_p;
    double theta_p;
    double sv_p[2];

    double s_m;
    double theta_m;
    double sv_m[2];

    double vel_mag;
    double vel_theta;

    double corners[4][2];
    bool has_corners;

    double mu;
} aux_particle_t;


typedef struct s_element {
    double x1;
    double y1;
    double x2;
    double y2;
    double x3;
    double y3;
    double x4;
    double y4;

    double sxx;
    double sxy;
    double syy;
} element_t;

enum e_particle_variables {
    VAR_M=0, VAR_V,
    VAR_X, VAR_Y,
    VAR_VX, VAR_VY,
    VAR_SXX, VAR_SXY, VAR_SYY,
    VAR_TAU, VAR_GAMMAP,
    VAR_YIELD, VAR_MU, VAR_MAGEF, VAR_GAMMADOTP,
    /* calculated from primitives */
    VAR_RHO, VAR_SMOOTH_SXX, VAR_SMOOTH_SXY, VAR_SMOOTH_SYY,
    NUM_VAR_PARTICLES
};


#include "node.hpp"
#include "backgroundmesh.hpp"

BackgroundMesh backgroundMesh;

class GLGraphicsPrimitive
{
    public:
        virtual ~GLGraphicsPrimitive() { return; };
        virtual void draw(GLenum mode) = 0;
};

class GLGPDisc: public GLGraphicsPrimitive
{
    Point2f center;
    float r;
    size_t numSegments;
    
    public:
        GLGPDisc() : r(0.03), numSegments(10) { center = Point2f(0,0); return; }

        GLGPDisc(Point2f const &_center, float _r, size_t _numSegments = 10) :
            center(_center), r(_r), numSegments(_numSegments)
        {
            return;
        }

        void draw(GLenum mode = GL_POLYGON)
        {
            //from http://slabode.exofire.net/circle_draw.shtml
            float cx = center.getCoordinate(0);
            float cy = center.getCoordinate(1);
            float theta = 2 * M_PI / float(numSegments); 
            float c = cosf(theta);//precalculate the sine and cosine
            float s = sinf(theta);
            float t;

            float x = r;//we start at angle = 0 
            float y = 0; 
            
            glBegin(mode); 
            for(size_t i = 0; i < numSegments; i++) 
            { 
                glVertex3f(x + cx, y + cy, -1.001f);//output vertex 
                
                //apply the rotation matrix
                t = x;
                x = c * x - s * y;
                y = s * t + c * y;
            } 
            glEnd();
            return;
        }

        void setCenter(Point2f &p) { center = p; }
        void setCenter(float x, float y) { center.setCoordinate(0, x); center.setCoordinate(1, y); }

};

void DrawDisc(GLenum mode, float cx, float cy, float r, int num_segments)
{
    //from http://slabode.exofire.net/circle_draw.shtml
    float theta = 2 * M_PI / float(num_segments);
    float c = cosf(theta);//precalculate the sine and cosine
    float s = sinf(theta);
    float t;
    float x = r;//we start at angle = 0
    float y = 0;
    glBegin(mode);
    for(int ii = 0; ii < num_segments; ii++)
    {
        glVertex3f(x + cx, y + cy, -1.001f);//output vertex
        //apply the rotation matrix
        t = x;
        x = c * x - s * y;
        y = s * t + c * y;
    }
    glEnd();
    return;
}

void calc_stress_eigenvalues(aux_particle_t *p)
{
    p->s_p = 0.5*(p->sxx+p->syy + sqrt((p->sxx-p->syy)*(p->sxx-p->syy) + 4*p->sxy*p->sxy));
    p->s_m = p->sxx+p->syy - p->s_p;
    return;
}

void calc_stress_eigenvectors(aux_particle_t *p)
{
    double x,y;

    y = p->s_p - p->sxx;
    x = p->sxy;
    p->theta_p = atan2(y,x);
    p->theta_m = M_PI/2.0 + p->theta_p;

    p->sv_p[0] = cos(p->theta_p);
    p->sv_p[1] = sin(p->theta_p);

    p->sv_m[0] = cos(p->theta_m);
    p->sv_m[1] = sin(p->theta_m);
    return;
}


void calc_vel_vector(aux_particle_t *p) {
    p->vel_mag = hypot(p->y_t, p->x_t);
    p->vel_theta = atan2(p->y_t, p->x_t);
    return;
}

void inline draw_vector(double x0, double y0, double mag, double theta)
{
    // assume GL_LINES

    //main vector line
    glVertex3f(x0, y0, -1.0f);
    glVertex3f(x0 + mag*cos(theta), y0 + mag*sin(theta), -1.0f);

    //arrowheads
    glVertex3f(x0 + mag*cos(theta + M_PI_4)/2.0, y0 + mag*sin(theta + M_PI_4)/2.0, -1.0f);
    glVertex3f(x0 + mag*cos(theta), y0 + mag*sin(theta), -1.0f);
    glVertex3f(x0 + mag*cos(theta - M_PI_4)/2.0, y0 + mag*sin(theta - M_PI_4)/2.0, -1.0f);
    glVertex3f(x0 + mag*cos(theta), y0 + mag*sin(theta), -1.0f);
    return;
}

void calc_stress_eigenpairs(aux_particle_t *p)
{
    calc_stress_eigenvalues(p);
    calc_stress_eigenvectors(p);

//    std::cout << "Theta Plus: " << p->theta_p << std::endl;
//    std::cout << "Theta Minus: " << p->theta_m << std::endl;

    return;
}

void draw_boundaries()
{
    int i;
    
    // inverse of background... don't be silly and use neutral grays for the 
    // bgcolor.
    glColor3f(
        1.0f - g_state.bgcolor.r,
        1.0f - g_state.bgcolor.g,
        1.0f - g_state.bgcolor.b
    );
    for (i = 0; i < g_state.num_scene_lines; i++) {
        glVertex3f(g_state.scene_lines[4*i + 0], g_state.scene_lines[4*i + 1], -1.0f);
        glVertex3f(g_state.scene_lines[4*i + 2], g_state.scene_lines[4*i + 3], -1.0f);

        if (g_state.mirror_x) {
            glVertex3f(g_state.scene_lines[4*i + 0], -g_state.scene_lines[4*i + 1], -1.0f);
            glVertex3f(g_state.scene_lines[4*i + 2], -g_state.scene_lines[4*i + 3], -1.0f);
        }

        if (g_state.mirror_y) {
            glVertex3f(-g_state.scene_lines[4*i + 0], g_state.scene_lines[4*i + 1], -1.0f);
            glVertex3f(-g_state.scene_lines[4*i + 2], g_state.scene_lines[4*i + 3], -1.0f);
        }

        if (g_state.mirror_x && g_state.mirror_y) {
            glVertex3f(-g_state.scene_lines[4*i + 0], -g_state.scene_lines[4*i + 1], -1.0f);
            glVertex3f(-g_state.scene_lines[4*i + 2], -g_state.scene_lines[4*i + 3], -1.0f);
        }
    }

    return;
}

void parse_scene(double **s, int *n, int *mx, int *my, FILE *scene_file)
{
    char line[256];
    unsigned int i;

    double *scene_lines = NULL;
    double *old_scene_lines = NULL;
    int num_scene_lines = 0;

    double x1,y1,x2,y2;

    while (!feof(scene_file)) {
        fgets(line, sizeof(line)/sizeof(line[0]), scene_file);
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        for (i = 0; i < (sizeof(line)/sizeof(line[0])); i++) {
            if (line[i] == '\0') {
                break;
            }

            line[i] = toupper(line[i]);
        }

        /* parse mirroring */
        if (strcmp(line, "M X\n") == 0) {
            *mx = 1;
            continue;
        } else if (strcmp(line, "M Y\n") == 0) {
            *my = 1;
            continue;
        } else if (strcmp(line, "M X,Y\n") == 0) {
            *mx = 1;
            *my = 1;
            continue;
        }

        /* parse camera */
        if (line[0] == 'C') {
            sscanf(line, "C %lg,%lg,%lg",
                &(g_state.camera_view[0]),
                &(g_state.camera_view[1]),
                &(g_state.camera_view[2]));
        }

        /* parse bounding lines */
        if (line[0] == 'L') {
            sscanf(line, "L %lg,%lg,%lg,%lg", &x1, &y1, &x2, &y2);
            num_scene_lines++;
            old_scene_lines = scene_lines;
            scene_lines = (double *)malloc(4 * sizeof(double) * num_scene_lines);
            memcpy(scene_lines, old_scene_lines, 4 * sizeof(double) * (num_scene_lines - 1));
            free(old_scene_lines);
            scene_lines[4 * num_scene_lines - 4] = x1;
            scene_lines[4 * num_scene_lines - 3] = y1;
            scene_lines[4 * num_scene_lines - 2] = x2;
            scene_lines[4 * num_scene_lines - 1] = y2;
        }
    }

    *s = scene_lines;
    *n = num_scene_lines;

    return;
}

void parse_colormap(cm_t *cm, FILE *cm_file)
{
    char line[256];
    unsigned int i;

    rgba_t *old_cm = NULL;
    float *old_anchors = NULL;
    int num_colors = 0;

    float f;
    int r, g, b, a;
    float rf, gf, bf, af;
    int h;
    bool set_color = 0;

    while (!feof(cm_file)) {
        fgets(line, sizeof(line)/sizeof(line[0]), cm_file);

        /* skip comment lines (starting with #) and blank lines. */
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        for (i = 0; i < (sizeof(line)/sizeof(line[0])); i++) {
            if (line[i] == '\0') {
                break;
            }

            line[i] = toupper(line[i]);
        }

        set_color = false;

        /* parse anchor/color pairs */
        if (line[0] == 'I') {
            sscanf(line, "I,%g,%d,%d,%d,%d", &f, &r, &g, &b, &a);
            rf = r / 255.0;
            gf = g / 255.0;
            bf = b / 255.0;
            af = a / 255.0;
            set_color = true;
        }
        
        if (line[0] == 'F') {
            sscanf(line, "F,%g,%g,%g,%g,%g", &f, &rf, &gf, &bf, &af);
            set_color = true;
        }

        if (line[0] == 'H') {
            sscanf(line, "H,%g,%x", &f, &h);
            rf = ((h >> 24) & 0xFF) / 255.0;
            gf = ((h >> 16) & 0xFF) / 255.0;
            bf = ((h >> 8) & 0xFF) / 255.0;
            af = ((h >> 0) & 0xFF) / 255.0;
            set_color = true;
        }

        if (set_color) {
            /* rf, gf, bf, af were set in one of the above blocks. */
            num_colors++;
            old_cm = cm->colors;
            old_anchors = cm->anchors;
            cm->colors = (rgba_t *)malloc(sizeof(rgba_t) * num_colors);
            cm->anchors = (float *)malloc(sizeof(float) * num_colors);
            memcpy(cm->colors, old_cm, sizeof(rgba_t) * (num_colors - 1));
            memcpy(cm->anchors, old_anchors, sizeof(float) * (num_colors - 1));
            free(old_cm);
            free(old_anchors);
            cm->colors[num_colors-1].r = rf;
            cm->colors[num_colors-1].g = gf;
            cm->colors[num_colors-1].b = bf;
            cm->colors[num_colors-1].a = af;
            RESTRICT_VALUE(cm->colors[num_colors-1].r, 0.0, 1.0);
            RESTRICT_VALUE(cm->colors[num_colors-1].g, 0.0, 1.0);
            RESTRICT_VALUE(cm->colors[num_colors-1].b, 0.0, 1.0);
            RESTRICT_VALUE(cm->colors[num_colors-1].a, 0.0, 1.0);
            cm->anchors[num_colors-1] = f;
            RESTRICT_VALUE(cm->anchors[num_colors-1], 0.0, 1.0);
        }
    }

    /* make sure bounds are sensible */
    cm->anchors[0] = 0.0;
    cm->anchors[num_colors - 1] = 1.0;
    cm->num_colors = num_colors;

    return;
}

class Colorbar
{
    private:
        Renderer<2> *renderer;
        Colormap *colormap;
        size_t gradation;
        float x_bottomleft, y_bottomleft;
        float width, height;

    public:
        Colorbar(Renderer<2> *_r, Colormap *_cm, size_t _grad, float _x, float _y, float _w, float _h)
            : renderer(_r), colormap(_cm), gradation(_grad),
            x_bottomleft(_x), y_bottomleft(_y), width(_w), height(_h)
        {
            return;
        }

};

void draw_colorbar(cm_t *cm, int gradation, float xl, float yl, float w, float h)
{
    float r, g, b;
    float delta;
    float xq0, yq0;
    int i;

    if (gradation <= 0) {
        return;
    } else {
        delta = 1.0f / gradation;
    }

    glBegin(GL_QUADS);
    for (i = 0; i < gradation; i++) {
        apply_colormap(cm, i * delta, &r, &g, &b);
        glColor3f(r, g, b);
        xq0 = xl;
        yq0 = yl + h*i*delta;
        glVertex3f(xq0, yq0, -1.0f);
        glVertex3f(xq0 + w, yq0, -1.0f);
        glVertex3f(xq0 + w, yq0 + delta*h, -1.0f);
        glVertex3f(xq0, yq0 + delta*h, -1.0f);
    }
    glEnd();

    return;
}

void tga_screendump(char *destFile, short W, short H)
{
    FILE   *out = fopen(destFile, "w");

    if (out == NULL) {
        std::cerr << "Couldn't open file " << destFile << "." << std::endl;
        return;
    }

    char   pixel_data[3*W*H];
    short  TGAhead[] = {0, 2, 0, 0, 0, 0, W, H, 24};
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, W, H, GL_BGR, GL_UNSIGNED_BYTE, pixel_data);
    fwrite(&TGAhead, sizeof(TGAhead), 1, out);
    fwrite(pixel_data, 3*W*H, 1, out);
    fclose(out);
}

/*
    Modification of PNG writing code from:
    http://www.labbookpages.co.uk/software/imgProc/files/libPNG/makePNG.c
*/

int png_screendump(char* filename, int width, int height, char* title)
{
    int code = 0;
    char titlekey[] = "Title";
    FILE *fp;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep row = NULL;

    // Open file for writing (binary mode)
    fp = fopen(filename, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open file %s for writing\n", filename);
        code = 1;
        goto finalise;
    }

    // Initialize write structure
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        fprintf(stderr, "Could not allocate write struct\n");
        code = 1;
        goto finalise;
    }

    // Initialize info structure
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        fprintf(stderr, "Could not allocate info struct\n");
        code = 1;
        goto finalise;
    }

    // Setup Exception handling
    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error during png creation\n");
        code = 1;
        goto finalise;
    }

    png_init_io(png_ptr, fp);

    // Write header (8 bit colour depth)
    png_set_IHDR(png_ptr, info_ptr, width, height,
	        8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
	        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    // Set title
    if (title != NULL) {
        png_text title_text;
        title_text.compression = PNG_TEXT_COMPRESSION_NONE;
        title_text.key = titlekey;
        title_text.text = title;
        png_set_text(png_ptr, info_ptr, &title_text, 1);
    }

    png_write_info(png_ptr, info_ptr);
    png_write_flush(png_ptr);

    // Allocate memory for one row (3 bytes per pixel - RGB)
    row = (png_bytep) malloc(3 * width * sizeof(png_byte));

    // Write image data
    for (int y = 0; y < height; y++) {
        glReadPixels(0, height-y-1, width, 1, GL_RGB, GL_UNSIGNED_BYTE, row);
        png_write_row(png_ptr, row);
    }

    // End write
    png_write_end(png_ptr, NULL);
    png_write_flush(png_ptr);

    finalise:
    if (fp != NULL) fclose(fp);
    if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
    if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    if (row != NULL) free(row);

    return code;
}

void next_element(FILE *fp, element_t *element)
{
    char s[16384];
    char *tok;
    double d;
    int i;

    fgets(s, sizeof(s)/sizeof(char), fp);

    i = 0;
    tok = strtok(s, " ,");
    while (tok != NULL) {
        sscanf(tok, "%lg", &d);
//        printf("%d: %lg\n", i, d);
        switch (i) {
            case 0:
                element->x1 = d;
                break;
            case 1:
                element->y1 = d;
                break;
            case 2:
                element->x2 = d;
                break;
            case 3:
                element->y2 = d;
                break;
            case 4:
                element->x3 = d;
                break;
            case 5:
                element->y3 = d;
                break;
            case 6:
                element->x4 = d;
                break;
            case 7:
                element->y4 = d;
                break;
            case 8:
                element->sxx = d;
                break;
            case 9:
                element->sxy = d;
                break;
            case 10:
                element->syy = d;
                break;
        }
        tok = strtok(NULL, " ,");
        i++;
    }

    return;
}

void next_particle(FILE *fp, aux_particle_t *particle)
{
    char s[16384];
    char *tok;
    double d;
    int i;

    fgets(s, sizeof(s)/sizeof(char), fp);

    particle->has_corners = false;

    i = 0;
    tok = strtok(s, " ,");
    while (tok != NULL) {
        sscanf(tok, "%lg", &d);
//        printf("%d: %lg\n", i, d);
        switch (i) {
            case 0:
                particle->m = d;
                break;
            case 1:
                particle->v = d;
                break;
            case 2:
                particle->x = d;
                break;
            case 3:
                particle->y = d;
                break;
            case 4:
                particle->x_t = d;
                break;
            case 5:
                particle->y_t = d;
                break;
            case 6:
                particle->sxx = d;
                break;
            case 7:
                particle->sxy = d;
                break;
            case 8:
                particle->syy = d;
                break;
            case 9:
                particle->ux = d;
                break;
            case 10:
                particle->uy = d;
                break;
            case 11:
                particle->gammap = d;
                break;
            case 12:
                particle->color = d;
                break;
            case 13:
                particle->magEf = d;
                break;
            case 14:
                particle->active = d;
                break;
            case 15:
                particle->corners[0][0] = d;
                particle->has_corners = true;
                break;
            case 16:
                particle->corners[0][1] = d;
                break;
            case 17:
                particle->corners[1][0] = d;
                break;
            case 18:
                particle->corners[1][1] = d;
                break;
            case 19:
                particle->corners[2][0] = d;
                break;
            case 20:
                particle->corners[2][1] = d;
                break;
            case 21:
                particle->corners[3][0] = d;
                break;
            case 22:
                particle->corners[3][1] = d;
                break;
        }
        tok = strtok(NULL, " ,");
        i++;
    }

    if (i <= 14) {
        particle->active = 1.0f;
    }

    if (i >= 14) {
        double p_t;
        double t0xx;
        double t0xy;
        double t0yy;
        double tau_t;
        p_t = -0.5 * (particle->sxx + particle->syy);
        t0xx = particle->sxx + p_t;
        t0xy = particle->sxy;
        t0yy = particle->syy + p_t;
        tau_t = sqrt(0.5*(t0xx*t0xx + 2*t0xy*t0xy + t0yy*t0yy));
        particle->mu = tau_t / (fmax(p_t, 0));
        if (p_t <= 1e-8) {
            particle->mu = 0.64;
        }
    }

    return;
}

aux_particle_t *next_frame(FILE *fp, int *num_particles)
{
    int i;
    aux_particle_t *particles = NULL;

    if (3 != fscanf(fp, "%d %lg %d\n", &current_frame, &current_time, num_particles)) {
        return NULL;
    }
//    fprintf(stdout, "Read Frame: %d, #Particles = %d\n", current_frame, *num_particles);
    
    particles = (aux_particle_t *)malloc(sizeof(aux_particle_t) * *num_particles);
    
    for (i = 0; i < *num_particles; i++) {
        next_particle(fp, &(particles[i]));
    }

    return particles;
}

element_t *next_element_frame(FILE *fp, int *num_elements)
{
    int i;
    element_t *elements = NULL;

    if (3 != fscanf(fp, "%d %lg %d\n", &current_frame, &current_time, num_elements)) {
        return NULL;
    }
//    fprintf(stdout, "Read Frame: %d, #Particles = %d\n", current_frame, *num_particles);
    
    elements = (element_t *)malloc(sizeof(element_t) * *num_elements);
    
    for (i = 0; i < *num_elements; i++) {
        next_element(fp, &(elements[i]));
    }

    return elements;
}

void apply_colormap(cm_t *cm, float val, float *r, float *g, float *b)
{
    int i,j;
    float delta, mix;

    if (cm->num_colors == 0) {
        *r = 0.5;
        *g = 0.5;
        *b = 0.5;
    } else if (cm->num_colors == 1) {
        *r = cm->colors[0].r;
        *g = cm->colors[0].g;
        *b = cm->colors[0].b;
    } else {
        RESTRICT_VALUE(val, 0.0, 1.0);
        for (i = 0; i < cm->num_colors; i++) {
            if (cm->anchors[i] >= val) {
                break;
            }
        }

        //if this is the first index, we are at the first value in the colormap
        if (i == 0) {
            *r = cm->colors[i].r;
            *g = cm->colors[i].g;
            *b = cm->colors[i].b;
        } else {
            j = i - 1;
            delta = cm->anchors[i] - cm->anchors[j];
            mix = (val - cm->anchors[j]) / delta;
            *r = (1 - mix) * cm->colors[j].r + mix * cm->colors[i].r;
            *g = (1 - mix) * cm->colors[j].g + mix * cm->colors[i].g;
            *b = (1 - mix) * cm->colors[j].b + mix * cm->colors[i].b;
        }
    }
    return;
}

void matlab_colormap_(float min, float max, float val, rgba_t *rgba)
{
    RESTRICT_VALUE(val, min, max);
}

void matlab_colormap(float h, float *r, float *g, float *b)
{
    // h, r, g, b from [0, 1]
    //code from http://www.cs.rit.edu/~ncs/color/t_convert.html
    int i;
    float f, p, q, t;
//    h = 6.0f*h;
    h = 0.75 * 6.0f * h;
    i = floor( h );
    f = h - i;
    p = 0;
    q = 1 - f;
    t = f;
    switch(i) {
        case 0:
            *r = 1;
            *g = t;
            *b = p;
            break;
        case 1:
            *r = q;
            *g = 1;
            *b = p;
            break;
        case 2:
            *r = p;
            *g = 1;
            *b = t;
            break;
        case 3:
            *r = p;
            *g = q;
            *b = 1;
            break;
        case 4:
            *r = t;
            *g = p;
            *b = 1;
            break;
        default:
            *r = 1;
            *g = p;
            *b = q;
            break;
    }
}

void draw_stress_tensor(double scaling,
    double eigval0, double eigval1,
    double eigang0, double eigang1)
{
    return;
}

int draw_particles(void)
{
//    const int points = 100000;
    int i, j;
    int c_idx;
    float r, g, b, hue;

    static aux_particle_t *particles = NULL;
    static aux_particle_t *previous_particles = NULL;
    static int prev_frame_time = 0;
    static int curr_frame_time = 0;
    aux_particle_t *ptmp = NULL;
    static int np;

    float data_max;
    float data_min;
    float data_delta;

    char title[1024];
    char fps_counter[32];

    double p;
    double scale;

    if (!feof(g_state.data_file)) {
        ptmp = next_frame(g_state.data_file, &np);
        if (ptmp != NULL) {
            if (previous_particles != NULL) {
                free(previous_particles);
            }
            prev_frame_time = curr_frame_time;
            curr_frame_time = current_frame;
            previous_particles = particles;
            particles = ptmp;
            ptmp = NULL;
        }
//        printf("Drawing %d particles.\n", np);
    } else {
        g_state.paused = 1;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    /* Autoscale the data if needed. */
    if (g_state.data_autoscale) {
//        p = particles[0].m / particles[0].v;
        p = -0.5 * (particles[0].sxx + particles[0].syy);
        data_max = p;
        data_min = p;
        for (i = 1; i < np; i++) {
            p = -0.5 * (particles[i].sxx + particles[i].syy);
            if (p > data_max) {
                data_max = p;
            }
            if (p < data_min) {
                data_min = p;
            }
        }
    } else {
        data_max = g_state.data_max;
        data_min = g_state.data_min;
    }

    data_delta = data_max - data_min;

    /* Scale stress tensor crosses if needed. */
    if (g_state.draw_stress_tensor) {

        for (i = 0; i < np; i++) {
            calc_stress_eigenpairs(&particles[i]);
        }

//        g_state.principal_stress_max = particles[0].s_p;
//        g_state.principal_stress_min = particles[0].s_m;

//        g_state.abs_ps_max = abs(particles[0].s_p);
//        g_state.abs_ps_min = abs(particles[0].s_m);

//        if (g_state.abs_ps_min > g_state.abs_ps_max) {
//            double tmp = g_state.abs_ps_min;
//            g_state.abs_ps_min = g_state.abs_ps_max;
//            g_state.abs_ps_max = tmp;
//        }

//        for (i = 1; i < np; i++) {
//            if (!particles[i].active) {
//                continue;
//            }

//            if (particles[i].s_m < g_state.principal_stress_min) {
//                g_state.principal_stress_min = particles[i].s_m;
//            }
//            if (particles[i].s_p > g_state.principal_stress_max) {
//                g_state.principal_stress_max = particles[i].s_p;
//            }

//            if (abs(particles[i].s_p) > g_state.abs_ps_max) {
//                g_state.abs_ps_max = abs(particles[i].s_p);
//            }
//            if (abs(particles[i].s_m) > g_state.abs_ps_max) {
//                g_state.abs_ps_max = abs(particles[i].s_m);
//            }

//            if (abs(particles[i].s_p) < g_state.abs_ps_min) {
//                g_state.abs_ps_min = abs(particles[i].s_p);
//            }
//            if (abs(particles[i].s_m) < g_state.abs_ps_min) {
//                g_state.abs_ps_min = abs(particles[i].s_m);
//            }
//        }

//        g_state.principal_stress_delta = g_state.principal_stress_max -
//            g_state.principal_stress_min;
//        g_state.abs_ps_delta = g_state.abs_ps_max -
//            g_state.abs_ps_min;
    }

//    g_state.abs_ps_delta = 500;
//    g_state.abs_ps_min = 1000;
//    g_state.abs_ps_max = 1500;

//    glTranslatef(-0.5f, -0.5f,-0.0f);
    glTranslatef(g_state.camera_view[0], g_state.camera_view[1],
        g_state.camera_view[2]);
    glPointSize(g_state.particle_size);
//    glBegin(GL_POINTS);

    /* compute smoothed stresses */
#ifdef SMOOTH
    backgroundMesh.clear();

    for (i = 0; i < np; i++) {
        if (!particles[i].active) {
            continue;
        }

        int elem = which_element(particles[i].x, particles[i].y, bgsize);
        if (elem < 0 || elem >= (bgsize - 1) * (bgsize - 1)) {
            continue;
        }
        int list[4];
        element_to_node_list(&list[0], elem, bgsize);

        for (int k = 0; k < 4; k++) {
            backgroundMesh[list[k]].CopyStressFromParticle(&(particles[i]));
        }
    }

    backgroundMesh.rescaleStress();

    for (i = 0; i < np; i++) {
        if (!particles[i].active) {
            continue;
        }
        particles[i].sxx = 0;
        particles[i].sxy = 0;
        particles[i].syy = 0;

        int elem = which_element(particles[i].x, particles[i].y, bgsize);
        if (elem < 0 || elem >= (bgsize - 1) * (bgsize - 1)) {
            continue;
        }
        int list[4];
        element_to_node_list(&list[0], elem, bgsize);
        for (int k = 0; k < 4; k++) {
            backgroundMesh[list[k]].CopyStressToParticle(&(particles[i]));
        }
    }
#endif

    for (i = 0; i < np; i++) {

        // fast draw
//        if (i % 16 != 0) {
//            continue;
//        }

        if (!particles[i].active) {
//            printf("%d inactive\n", i);
            continue;
        }
//        data_max = 1.0f;
//        data_min = 0.0f;
//        data_max = 1500;
//        data_min = 1000;

//        hue = sqrt(particles[i].ux * particles[i].ux + particles[i].uy * particles[i].uy);
//        hue = sqrt(particles[i].x_t * particles[i].x_t + particles[i].y_t * particles[i].y_t);
//        hue = (particles[i].m / particles[i].v > 1200)?(0.5):(0);

        switch (g_state.data_var) {
            case VAR_DISPLACEMENT:
                hue = hypot(particles[i].ux, particles[i].uy);
                break;
            case VAR_RHO:
                hue = particles[i].m / particles[i].v;
                break;
            case VAR_PRESSURE:
                hue = -0.5f*(particles[i].sxx + particles[i].syy);
                break;
            case VAR_VELOCITY:
                hue = hypot(particles[i].x_t, particles[i].y_t);
                break;
            case VAR_VX:
                hue = particles[i].x_t;
                break;
            case VAR_SXX:
                hue = particles[i].sxx;
                break;
            case VAR_SXY:
                hue = particles[i].sxy;
                break;
            case VAR_SYY:
                hue = particles[i].syy;
                break;
            case VAR_GAMMAP:
                hue = particles[i].gammap;
                break;
            case VAR_GAMMADOTP:
                if (previous_particles != NULL) {
                    hue = (particles[i].gammap - previous_particles[i].gammap)
                            / (curr_frame_time - prev_frame_time);
                } else {
                    hue = particles[i].gammap;
                }
                break;
            case VAR_MU:
                if (previous_particles != NULL) {
                    hue = (particles[i].gammap - previous_particles[i].gammap)
                            / (curr_frame_time - prev_frame_time);
                } else {
                    hue = particles[i].gammap;
                }
//                hue = hue * (0.05 * sqrt(2450)) / sqrt(-0.5f*(particles[i].sxx + particles[i].syy));
//                hue = 0.32 + 0.32 / (0.28 / (hue) + 1.0);
                hue = particles[i].mu;
                break;
            case VAR_MAGEF:
                hue = particles[i].magEf;
                break;
            case VAR_TAU:
                hue = sqrt(particles[i].sxy * particles[i].sxy + 0.5 * (particles[i].sxx - particles[i].syy) * (particles[i].sxx - particles[i].syy));
                break;
            case VAR_YIELD:
                hue = -0.5f*(particles[i].sxx + particles[i].syy);
                hue = sqrt(particles[i].sxy * particles[i].sxy + 0.5 * (particles[i].sxx - particles[i].syy) * (particles[i].sxx - particles[i].syy)) - tan((30.0*M_PI/180.0)) * hue;
                break;
            default:
                hue = hypot(particles[i].ux, particles[i].uy);
        }

        if (g_state.logscale) {
            if (hue <= 0) {
                hue = -20;
            } else {
                hue = log10(hue);
            }
        }

        if (hue > data_max) {
            hue = 1.0f;
        } else if (hue < data_min) {
            hue = 0.0f;
        } else {
            hue = (hue - data_min) / data_delta;
        }

        if (g_state.colormap.num_colors <= 0) {
            matlab_colormap(hue, &r, &g, &b);
        } else if (g_state.colormap.num_colors == 1) {
            r = g_state.colormap.colors[0].r;
            g = g_state.colormap.colors[0].g;
            b = g_state.colormap.colors[0].b;
        } else {
            apply_colormap(&(g_state.colormap), hue, &r, &g, &b);
//            if (hue >= 0.5) {
//                r = 153/256.0;
//                g = 255/256.0;
//                b = 0/256.0;
//            } else {
//                r = 102/256.0;
//                g = 0/256.0;
//                b = 255/256.0;
//            }
        }
        if (g_state.color_override == 0) {
            glColor3f(r, g, b);
        } else {
            //get override color index
            c_idx = 3 * i;
            r = cfg_getnfloat(g_state.cfg, "color-by-index", c_idx+0);
            g = cfg_getnfloat(g_state.cfg, "color-by-index", c_idx+1);
            b = cfg_getnfloat(g_state.cfg, "color-by-index", c_idx+2);
            glColor3f(r, g, b);
        }
#if 0
        // override for stripes!
        if ((i / (4 * 40) % 2) == 0) {
            //red
            glColor3f(228/256.0, 28/256.0, 28/256.0);
        } else {
            //gray
            glColor3f(153/256.0, 153/256.0, 153/256.0);
        }
#endif
//        glVertex3f(particles[i].x, particles[i].y, -1.0f);

#define NUM_SEGS 10
        DrawDisc(GL_POLYGON, particles[i].x, particles[i].y, g_state.particle_size * 1e-3, NUM_SEGS);

        if (particles[i].has_corners && (g_state.draw_glyphs != 0)) {
            glBegin(GL_LINE_LOOP);
            for (j = 0; j < 4; j++) {
                glVertex3f(particles[i].corners[j][0], particles[i].corners[j][1], -1.0f);
            }
            glEnd();
        }

        if (g_state.mirror_x) {
            glVertex3f(particles[i].x, -particles[i].y, -1.0f);
            DrawDisc(GL_POLYGON, particles[i].x, -particles[i].y, g_state.particle_size * 1e-3, NUM_SEGS);
        }

        if (g_state.mirror_y) {
            glVertex3f(-particles[i].x, particles[i].y, -1.0f);
            DrawDisc(GL_POLYGON, -particles[i].x, particles[i].y, g_state.particle_size * 1e-3, NUM_SEGS);
        }

        if (g_state.mirror_x && g_state.mirror_y) {
            glVertex3f(-particles[i].x, -particles[i].y, -1.0f);
            DrawDisc(GL_POLYGON, -particles[i].x, -particles[i].y, g_state.particle_size * 1e-3, NUM_SEGS);
        }

//        glColor3f(1.0 - g_state.bgcolor.r, 1.0 - g_state.bgcolor.g, 1.0 - g_state.bgcolor.b);
//        DrawCircle(particles[i].x, particles[i].y, g_state.particle_size * 1e-3, NUM_SEGS);
    }
//    glEnd();

    /* Draw simulation bounding lines. */
    glBegin(GL_LINES);
    draw_boundaries();
    glEnd();

    g_state.abs_ps_max = 400;
    /* Draw stress tensor crosses. */
    if (g_state.draw_stress_tensor) {
        glBegin(GL_LINES);

        /* major principal stress */
        glColor3f(1.0f, 1.0f, 1.0f);
        for (i = 0; i < np; i++) {
            if (!particles[i].active) {
                continue;
            }
            if (particles[i].s_p < 0) {
                glColor3f(1.0f, 0.0f, 1.0f);
            } else {
                glColor3f(0.5f, 0.0f, 0.5f);
            }
            scale = (abs(particles[i].s_p)) / g_state.abs_ps_max;
            glVertex3f(particles[i].x - CROSS_SIZE*scale*particles[i].sv_p[0], particles[i].y - CROSS_SIZE*scale*particles[i].sv_p[1], -1.0f);
            glVertex3f(particles[i].x + CROSS_SIZE*scale*particles[i].sv_p[0], particles[i].y + CROSS_SIZE*scale*particles[i].sv_p[1], -1.0f);

            if (g_state.mirror_y) {
                glVertex3f(-(particles[i].x - CROSS_SIZE*scale*particles[i].sv_p[0]), particles[i].y - CROSS_SIZE*scale*particles[i].sv_p[1], -1.0f);
                glVertex3f(-(particles[i].x + CROSS_SIZE*scale*particles[i].sv_p[0]), particles[i].y + CROSS_SIZE*scale*particles[i].sv_p[1], -1.0f);
            }

            /* minor principal stress */
            if (particles[i].s_m < 0) {
                glColor3f(0.0f, 1.0f, 0.0f);
            } else {
                glColor3f(0.0f, 0.5f, 0.0f);
            }
            scale = (abs(particles[i].s_m)) / g_state.abs_ps_max;
            glVertex3f(particles[i].x - CROSS_SIZE*scale*particles[i].sv_m[0], particles[i].y - CROSS_SIZE*scale*particles[i].sv_m[1], -1.0f);
            glVertex3f(particles[i].x + CROSS_SIZE*scale*particles[i].sv_m[0], particles[i].y + CROSS_SIZE*scale*particles[i].sv_m[1], -1.0f);

            if (g_state.mirror_y) {
                glVertex3f(-(particles[i].x - CROSS_SIZE*scale*particles[i].sv_m[0]), particles[i].y - CROSS_SIZE*scale*particles[i].sv_m[1], -1.0f);
                glVertex3f(-(particles[i].x + CROSS_SIZE*scale*particles[i].sv_m[0]), particles[i].y + CROSS_SIZE*scale*particles[i].sv_m[1], -1.0f);
            }

        }

        glEnd();
    }


    if (g_state.draw_velocity_vector) {
        glBegin(GL_LINES);
        for (i = 0; i < np; i++) {
            if (!particles[i].active) {
                continue;
            }
            calc_vel_vector(&particles[i]);
            scale = 1;

            glColor3f(1.0f, 1.0f, 1.0f);
            draw_vector(particles[i].x, particles[i].y, scale*particles[i].vel_mag, particles[i].vel_theta);

//            printf("vel_mag %lg, vel_theta = %lg\n", particles[i].vel_mag, particles[i].vel_theta);

            if (g_state.mirror_y) {
                draw_vector(-particles[i].x, particles[i].y, scale*particles[i].vel_mag, M_PI - particles[i].vel_theta);
            }
        }
        glEnd();
    }

#define XC 0.55
#define YC 0.1
#define W 0.05
#define H 0.8
    if (g_state.want_colorbar) {
        draw_colorbar(&(g_state.colormap), 40, XC, YC, W, H);

        /* draw with inverse of background */
        glColor3f(
            1.0f - g_state.bgcolor.r,
            1.0f - g_state.bgcolor.g,
            1.0f - g_state.bgcolor.b
        );
        glRasterPos3f(XC + W, YC - 0.02, -1.0f);
        snprintf(title, sizeof(title)/sizeof(title[0]),
            "%4.2f", data_min);
        ftglRenderLayout(small_left_layout, title, RENDER_ALL);

        glRasterPos3f(XC + W, YC + H, -1.0f);
        snprintf(title, sizeof(title)/sizeof(title[0]),
            "%4.2f", data_max);
        ftglRenderLayout(small_left_layout, title, RENDER_ALL);

        glColor3f(1.0f, 1.0f, 1.0f);
        snprintf(title, sizeof(title)/sizeof(title[0]),
            "[Frame %5.5d]\nTime: %8.5f", current_frame, current_time);
        snprintf(fps_counter, sizeof(fps_counter)/sizeof(fps_counter[0]),
            " \nFPS: [%6.2f]", current_fps);
    }

    glRasterPos3f(-0.5f-g_state.camera_view[0], -0.0f, -1.0f);
//    ftglRenderLayout(small_left_layout, fps_counter, RENDER_ALL);

    glRasterPos3f(-0.5f-g_state.camera_view[0], 1.0f, -1.0f);
//    ftglRenderLayout(centered_layout, title, RENDER_ALL);

    return true;
}

int draw_elements(void)
{
    int i;
    float r, g, b, hue;

    static element_t *elements = NULL;
    element_t *etmp = NULL;
    static int ne;

    float data_max;
    float data_min;
    float data_delta;

    char title[1024];
    char fps_counter[32];

    if (!feof(g_state.data_file)) {
        etmp = next_element_frame(g_state.data_file, &ne);
        if (etmp != NULL) {
            if (elements != NULL) {
                free(elements);
            }
            elements = etmp;
            etmp = NULL;
        }
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();


    glTranslatef(g_state.camera_view[0], g_state.camera_view[1], g_state.camera_view[2]);
    glPointSize(g_state.particle_size);
    glBegin(GL_QUADS);

    data_max = g_state.data_max;
    data_min = g_state.data_min;
    data_delta = data_max - data_min;
    double p = 0;

    for (i = 0; i < ne; i++) {
        if (g_state.data_var == VAR_SXY) {
            p = elements[i].sxy;
        } else if (g_state.data_var == VAR_SYY) {
            p = elements[i].syy;
        } else if (g_state.data_var == VAR_SXX) {
            p = elements[i].sxx;
        } else if (g_state.data_var == VAR_PRESSURE) {
            p = -0.5 * (elements[i].sxx + elements[i].syy);
        } else if (g_state.data_var == VAR_MU) {
            double p_t;
            double t0xx;
            double t0xy;
            double t0yy;
            double tau_t;
            p_t = -0.5 * (elements[i].sxx + elements[i].syy);
            t0xx = elements[i].sxx + p_t;
            t0xy = elements[i].sxy;
            t0yy = elements[i].syy + p_t;
            tau_t = sqrt(0.5f*(t0xx*t0xx + 2*t0xy*t0xy + t0yy*t0yy));
            p = tau_t / (fmax(p_t, 0) + 1e-4);
//            printf("%g\n", p);
        } else if (g_state.data_var == VAR_TAU) {
            p = sqrt(elements[i].sxy * elements[i].sxy + 0.5 * (elements[i].sxx - elements[i].syy) * (elements[i].sxx - elements[i].syy));
        }

        hue = p;

        if (hue > data_max) {
            hue = 1.0f;
        } else if (hue < data_min) {
            hue = 0.0f;
        } else {
            hue = (hue - data_min) / data_delta;
        }

        if (p == 0.0f) {
            continue;
        }

        if (g_state.colormap.num_colors <= 0) {
            matlab_colormap(hue, &r, &g, &b);
        } else if (g_state.colormap.num_colors == 1) {
            r = g_state.colormap.colors[0].r;
            g = g_state.colormap.colors[0].g;
            b = g_state.colormap.colors[0].b;
        } else {
            apply_colormap(&(g_state.colormap), hue, &r, &g, &b);
        }
        if (g_state.color_override == 0) {
            glColor3f(r, g, b);
        } else {
            //get override color index
            int c_idx = 3 * i;
            r = cfg_getnfloat(g_state.cfg, "color-by-index", c_idx+0);
            g = cfg_getnfloat(g_state.cfg, "color-by-index", c_idx+1);
            b = cfg_getnfloat(g_state.cfg, "color-by-index", c_idx+2);
            glColor3f(r, g, b);
        }

        glVertex3f(elements[i].x1, elements[i].y1, -1.0f);
        glVertex3f(elements[i].x2, elements[i].y2, -1.0f);
        glVertex3f(elements[i].x3, elements[i].y3, -1.0f);
        glVertex3f(elements[i].x4, elements[i].y4, -1.0f);
        if (g_state.mirror_y) {
            glVertex3f(-elements[i].x1, elements[i].y1, -1.0f);
            glVertex3f(-elements[i].x2, elements[i].y2, -1.0f);
            glVertex3f(-elements[i].x3, elements[i].y3, -1.0f);
            glVertex3f(-elements[i].x4, elements[i].y4, -1.0f);
        }
        if (g_state.mirror_x) {
            glVertex3f(elements[i].x1, -elements[i].y1, -1.0f);
            glVertex3f(elements[i].x2, -elements[i].y2, -1.0f);
            glVertex3f(elements[i].x3, -elements[i].y3, -1.0f);
            glVertex3f(elements[i].x4, -elements[i].y4, -1.0f);
        }
        if (g_state.mirror_x && g_state.mirror_y) {
            glVertex3f(-elements[i].x1, -elements[i].y1, -1.0f);
            glVertex3f(-elements[i].x2, -elements[i].y2, -1.0f);
            glVertex3f(-elements[i].x3, -elements[i].y3, -1.0f);
            glVertex3f(-elements[i].x4, -elements[i].y4, -1.0f);
        }
    }
    glEnd();

    glBegin(GL_LINES);
    draw_boundaries();
    glEnd();


    snprintf(title, sizeof(title)/sizeof(title[0]),
        "[Frame %5.5d] Time: %8.5f", current_frame, current_time);
    snprintf(fps_counter, sizeof(fps_counter)/sizeof(fps_counter[0]),
        " \nFPS: [%6.2f]", current_fps);

    glRasterPos3f(-0.5f-g_state.camera_view[0], -0.0f, -0.99f);
    ftglRenderLayout(small_left_layout, fps_counter, RENDER_ALL);

    glRasterPos3f(-0.5f-g_state.camera_view[0], 1.0f, -0.99f);
    ftglRenderLayout(centered_layout, title, RENDER_ALL);

    return true;
}

bool init_sdl(void)
{
    if( SDL_Init( SDL_INIT_EVERYTHING ) != 0 )
    {
        return false;
    }

    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

//    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
//    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, 1);

    // TODO: Add error check to this screen surface init
    screen = SDL_SetVideoMode( screen_width, screen_height, screen_bpp, SDL_OPENGL | SDL_HWSURFACE | SDL_RESIZABLE );

    return true;
}

void init_ftgl(std::vector<std::string> &fontlist)
{
    font = NULL;
    small_font = NULL;

    /* Create a pixmap font from a TrueType file. */
    bool got_valid_font = false;
    for (auto const &s: fontlist) {
        std::cout << "Attempting to load font " << s << "... ";
        font = ftglCreatePixmapFont(s.c_str());
        small_font = ftglCreatePixmapFont(s.c_str());
        if (font != NULL && small_font != NULL) {
            got_valid_font = true;
            std::cout << "Success." << std::endl;
            break;
        } else {
            // Something went wrong with one of the loads; cleanup if necessary.
            std::cout << "Failure." << std::endl;
            if (font != NULL) {
                ftglDestroyFont(font);
            }
            if (small_font != NULL) {
                ftglDestroyFont(small_font);
            }
        }
    }

    if (!got_valid_font) {
        std::cerr << "FATAL: Couldn't load any fonts." << std::endl;
        exit(0);
    }

    centered_layout = ftglCreateSimpleLayout();
    small_centered_layout = ftglCreateSimpleLayout();
    small_left_layout = ftglCreateSimpleLayout();

    ftglSetLayoutFont(centered_layout, font);
    ftglSetLayoutAlignment(centered_layout, ALIGN_CENTER);
    ftglSetLayoutLineLength(centered_layout, 800.0f);

    ftglSetLayoutFont(small_centered_layout, small_font);
    ftglSetLayoutAlignment(small_centered_layout, ALIGN_CENTER);
    ftglSetLayoutLineLength(small_centered_layout, 800.0f);

    ftglSetLayoutFont(small_left_layout, small_font);
    ftglSetLayoutAlignment(small_left_layout, ALIGN_LEFT);
    ftglSetLayoutLineLength(small_left_layout, 800.0f);

    /* Set the font size and render a small text. */
    ftglSetFontFaceSize(font, 72, 72);
    ftglSetFontFaceSize(small_font, 24, 72);
}

static void init_opengl(void)
{
    float aspect = (float)screen_width / (float)screen_height;
    glViewport(0, 0, screen_width, screen_height);
//    glMatrixMode(GL_PROJECTION);
//    glLoadIdentity();
//    gluPerspective(60.0, aspect, 0.1, 100.0);
//    glMatrixMode(GL_MODELVIEW);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0/aspect, 1.0/aspect, -2.0, 2.0);
    glMatrixMode(GL_MODELVIEW);
    glClearColor(g_state.bgcolor.r, g_state.bgcolor.g, g_state.bgcolor.b,
                    g_state.bgcolor.a);

    glDisable(GL_DEPTH_TEST);
}

template <typename pIterator, typename gIterator>
gIterator drawParticles(pIterator pbegin, pIterator pend, gIterator gbegin)
{
    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glColor3f(1,0,1);
    while (pbegin != pend) {
        Particle &p = *pbegin;
        if (p.isActive()) {
            (*gbegin).setCenter(p["x"], p["y"]);
            (*gbegin).draw();
            gbegin++;
        }
        pbegin++;
    }
    return gbegin; //now points to one beyond the last element
}

std ::vector<GLGPDisc> GLGPDiscs;

void heartbeat(void)
{
    int start_ticks;
    int delta;
    int delay;

    static bool inMouseDrag = false;
    static double initialX, initialY;

    SDL_Event event;

    while(1)
    {
        while(SDL_PollEvent(&event))
        {
            switch(event.type)
            {
                case SDL_KEYDOWN:
                    switch(event.key.keysym.sym)
                    {
                        case SDLK_ESCAPE:
                            std::cout << std::endl;
                            std::cout << "Bye." << std::endl;
                            exit(0);
                            break;
                        case SDLK_p:
                            g_state.paused ^= 1;
                            if (g_state.paused) {
                                std::cout << "Pausing Simulation." << std::endl;
                            }
                            break;
                        case SDLK_n:
                            if (g_state.paused) {
                                g_state.step_next = 1;
                            }
                            break;
                        case SDLK_r:
                            std::cout << "Rewinding framedata." << std::endl;
                            rewind(g_state.data_file);
                            break;
                        case SDLK_h:
                            g_state.wanted_fps = 30.0;
                            break;
                        case SDLK_q:
                            g_state.wanted_fps = 15.0;
                            break;
                        case SDLK_t:
                            g_state.wanted_fps = 6.0;
                            break;
                        case SDLK_f:
                            g_state.wanted_fps = 60.0;
                            break;
                        case SDLK_d:
                            g_state.wanted_fps = 120.0;
                            break;
                        default:
                            break;
                    }
                    break;

                case SDL_QUIT:
                    std::cout << std::endl;
                    std::cout << "Bye." << std::endl;
                    exit(0);
                    break;

                case SDL_VIDEORESIZE:
                    screen = SDL_SetVideoMode( event.resize.w, event.resize.h, screen_bpp, SDL_OPENGL | SDL_HWSURFACE | SDL_RESIZABLE );
                    screen_width = event.resize.w;
                    screen_height = event.resize.h;
                    init_opengl();
                    std::cout << "Resized to width: " << event.resize.w << " height: " << event.resize.h << std::endl;
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    inMouseDrag = true;
                    initialX = event.button.x;
                    initialY = event.button.y;
                    break;

                case SDL_MOUSEMOTION:
                    if (inMouseDrag) {
                        float aspect = screen->w / screen->h;
                        g_state.camera_view[0] += 2.0 * (event.motion.x - initialX) / (screen->w);
                        g_state.camera_view[1] += -2.0 * aspect * (event.motion.y - initialY) / (screen->h);
                        glTranslatef(g_state.camera_view[0], g_state.camera_view[1],
                            g_state.camera_view[2]);
                        initialX = event.motion.x;
                        initialY = event.motion.y;
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    inMouseDrag = false;
                    break;

                default:
                    break;
            }
        }

        if (g_state.paused) {
            usleep(10000);
            if (g_state.step_next) {
                g_state.step_next = 0;
            } else {
                continue;
            }
        }

        start_ticks = SDL_GetTicks();
        SDL_LockSurface(screen);
        if (g_state.is_element_file) {
            draw_elements();
        } else {
//            auto particles = g_state.reader->nextParticles();
//            if (GLGPDiscs.size() < particles.size()) {
//                GLGPDiscs.resize(particles.size());
//            }
//            drawParticles(particles.begin(), particles.end(), GLGPDiscs.begin());

//            for (auto const &p : particles) {
//                if (p.isActive()) {
//                    auto g = GLGPDisc(Point2f(p["x"], p["y"]), 0.01);
//                    g.draw();
//                }
//            }
            draw_particles();
        }
        SDL_UnlockSurface(screen);
        delta = SDL_GetTicks() - start_ticks;
        delay = ((1000.0f / g_state.wanted_fps) - delta);

        if (delay > 0) {
            SDL_Delay(delay);
        }
        delta = SDL_GetTicks() - start_ticks;
        current_fps = 1000.0f / delta;
        printf("Frame: [%05d] Time: [%8.5f] FPS: [%6.2f]\r", current_frame, current_time, current_fps);
        snprintf(g_state.wm_title, sizeof(g_state.wm_title) / sizeof(g_state.wm_title[0]),
            "%s[%05d]: %8.5fs", g_state.loaded_file_path, current_frame, current_time);
        SDL_WM_SetCaption(g_state.wm_title, g_state.wm_title);
        fflush(stdout);

        SDL_GL_SwapBuffers();

        if (g_state.write_frames && !feof(g_state.data_file)) {
//            snprintf(pngfile, sizeof(pngfile)/sizeof(pngfile[0]), "%s_%d.tga", pngbase, current_frame);
//            tga_screendump(pngfile, screen->w, screen->h);
            snprintf(pngfile, sizeof(pngfile)/sizeof(pngfile[0]), "%s_%d.png", pngbase, current_frame);
            png_screendump(pngfile, screen->w, screen->h, g_state.wm_title);

    //        printf("Screen: %p\n", screen);
    //        printf("Screen->w: %d\n", screen->w);
    //        printf("Screen->h: %d\n", screen->h);
    //        fflush(stdout);`
    //        output_surf = SDL_CreateRGBSurface(screen->flags, screen->w, screen->h, screen->format->BitsPerpixel, rmask, gmask, bmask, 0);
    //        SDL_BlitSurface(screen, NULL, output_surf, NULL);
    //        snprintf(pngfile, sizeof(pngfile)/sizeof(pngfile[0]), "%s_%d.png", pngbase, current_frame);
    //        png_save_surface(pngfile, screen);
    //        snprintf(pngfile, sizeof(pngfile)/sizeof(pngfile[0]), "%s_%d.bmp", pngbase, current_frame);
    //        SDL_SaveBMP(output_surf, pngfile);
    //        SDL_FreeSurface(output_surf);
        } else if (g_state.write_frames && feof(g_state.data_file)) {
            /* exit when done with file if we are dumping frames */
            exit(0);
        }

    }
}

int main(int argc, char* argv[])
{
    cfg_opt_t opts[] =
    {
        CFG_STR_LIST("fonts", "{ /usr/share/cups/fonts/FreeMono.ttf, /usr/share/fonts/google-droid/DroidSansMono.ttf }", CFGF_NONE),
        CFG_INT("override-particle-colors", 0, CFGF_NONE),
        CFG_INT("draw-glyphs", 1, CFGF_NONE),
        CFG_FLOAT_LIST("color-by-index", "{0, 0, 0}", CFGF_NONE),
        CFG_FUNC("include", cfg_include),
        CFG_END()
    };

    int opt = 0;
    int leftover_argc;
    char **leftover_argv;

    FILE *scene_file = NULL;
    FILE *colormap_file = NULL;

    std::ofstream::sync_with_stdio(false);
    std::ifstream::sync_with_stdio(false);
    std::ios::sync_with_stdio(false);

    if( init_sdl() != false )
    {
        std::cout << "SDL Init Successful." << std::endl;
    }

//    Colormap default_colormap;
//    std::cout << default_colormap << std::endl;
//    std::cout << default_colormap.interpolatedColor(-1) << std::endl;
//    std::cout << default_colormap.interpolatedColor(0) << std::endl;
//    std::cout << default_colormap.interpolatedColor(0.87) << std::endl;
//    std::cout << default_colormap.interpolatedColor(1) << std::endl;
//    std::cout << default_colormap.interpolatedColor(2) << std::endl;

    /* Command line option defaults */
    g_state.data_file = NULL;
    g_state.is_element_file = 0;
    g_state.data_min = 0;
    g_state.data_max = 1;
    g_state.data_autoscale = 0;
    g_state.data_var = 0;
    g_state.write_frames = 0;
    g_state.particle_size = PARTICLE_SIZE;

    /* State defaults. */
    g_state.paused = 0;
    g_state.step_next = 0;

    g_state.mirror_x = 0;
    g_state.mirror_y = 0;
    g_state.num_scene_lines = 0;
    g_state.scene_lines = NULL;

    g_state.camera_view[0] = -0.5f;
    g_state.camera_view[1] = -0.5f;
    g_state.camera_view[2] = -0.0f;

    g_state.wanted_fps = 60.0f;
    g_state.draw_stress_tensor = 0;
    g_state.draw_velocity_vector = 0;

    g_state.bgcolor.r = 0.0f;
    g_state.bgcolor.g = 0.0f;
    g_state.bgcolor.b = 0.0f;
    g_state.bgcolor.a = 1.0f;

    g_state.colormap.colors = NULL;
    g_state.colormap.anchors = NULL;
    g_state.colormap.num_colors = 0;

    g_state.want_colorbar = 0;

    g_state.logscale = 0;

    /* configuration file parsing XXX remove hardcoding!*/
    g_state.cfg = cfg_init(opts, CFGF_NONE);
    if (cfg_parse(g_state.cfg, "visualization.cfg") == CFG_PARSE_ERROR) {
        fprintf(stderr, "Fatal -- cannot parse configuration file '%s'.\n",
            "visualization.cfg");
        exit(127);
    }

    g_state.color_override = cfg_getint(g_state.cfg, "override-particle-colors");
    g_state.draw_glyphs = cfg_getint(g_state.cfg, "draw-glyphs");

    opt = getopt(argc, argv, g_optstring);

    while (opt != -1) {
        switch (opt) {
            case 'e':
                g_state.is_element_file = 1;
                break;
            case 'a':
                g_state.data_autoscale = 1;
                break;
            case 'b':
                if (strcmp(optarg, "white") == 0) {
                    g_state.bgcolor.r = 1.0;
                    g_state.bgcolor.g = 1.0;
                    g_state.bgcolor.b = 1.0;
                    g_state.bgcolor.a = 1.0;
                } else if (strcmp(optarg, "black") == 0) {
                    g_state.bgcolor.r = 0.0;
                    g_state.bgcolor.g = 0.0;
                    g_state.bgcolor.b = 0.0;
                    g_state.bgcolor.a = 1.0;
                } else {
                    sscanf(optarg, "%g,%g,%g,%g",
                        &(g_state.bgcolor.r),
                        &(g_state.bgcolor.g),
                        &(g_state.bgcolor.b),
                        &(g_state.bgcolor.a)
                    );
                    RESTRICT_VALUE(g_state.bgcolor.r, 0.0, 1.0);
                    RESTRICT_VALUE(g_state.bgcolor.g, 0.0, 1.0);
                    RESTRICT_VALUE(g_state.bgcolor.b, 0.0, 1.0);
                    RESTRICT_VALUE(g_state.bgcolor.a, 0.0, 1.0);
                }
                break;
            case 'c':
                scene_file = fopen(optarg, "r");
                if (scene_file == NULL) {
                    std::cout << "Can't open scene file:" << optarg << std::endl;
                } else {
                    parse_scene(
                        &(g_state.scene_lines), &(g_state.num_scene_lines),
                        &(g_state.mirror_x), &(g_state.mirror_y),
                        scene_file);
                }
                break;
            case 'd':
                g_state.particle_size = atof(optarg);
                printf("Particle size set to: %g.\n", g_state.particle_size);
                break;
            case 'm':
                colormap_file = fopen(optarg, "r");
                if (colormap_file == NULL) {
                    std::cout << "Can't open colormap file:" << optarg << std::endl;
                } else {
                    parse_colormap(&(g_state.colormap), colormap_file);
                }
                break;
            case 'l':
                g_state.data_min = atof(optarg);
                printf("Lower bound on variable: %g.\n", g_state.data_min);
                break;
            case 'u':
                g_state.data_max = atof(optarg);
                printf("Upper bound on variable: %g.\n", g_state.data_max);
                break;
            case 'p':
                if (strcmp(optarg, "u") == 0) {
                    g_state.data_var = VAR_DISPLACEMENT;
                } else if (strcmp(optarg, "v") == 0) {
                    g_state.data_var = VAR_VELOCITY;
                } else if (strcmp(optarg, "vx") == 0) {
                    g_state.data_var = VAR_VX;
                } else if (strcmp(optarg, "rho") == 0) {
                    g_state.data_var = VAR_RHO;
                } else if (strcmp(optarg, "p") == 0) {
                    g_state.data_var = VAR_PRESSURE;
                } else if (strcmp(optarg, "sxx") == 0) {
                    g_state.data_var = VAR_SXX;
                } else if (strcmp(optarg, "sxy") == 0) {
                    g_state.data_var = VAR_SXY;
                } else if (strcmp(optarg, "syy") == 0) {
                    g_state.data_var = VAR_SYY;
                } else if (strcmp(optarg, "tau") == 0) {
                    g_state.data_var = VAR_TAU;
                } else if (strcmp(optarg, "yield") == 0) {
                    g_state.data_var = VAR_YIELD;
                } else if (strcmp(optarg, "gammap") == 0) {
                    g_state.data_var = VAR_GAMMAP;
                } else if (strcmp(optarg, "gammadotp") == 0) {
                    g_state.data_var = VAR_GAMMADOTP;
                } else if (strcmp(optarg, "mu") == 0) {
                    g_state.data_var = VAR_MU;
                } else if (strcmp(optarg, "magef") == 0) {
                    g_state.data_var = VAR_MAGEF;
                } else {
                    g_state.data_var = VAR_DISPLACEMENT;
                }
                printf("Using variable %d.\n", g_state.data_var);
                break;
            case 's':
                g_state.draw_stress_tensor = 1;
                printf("Drawing stress tensor.\n");
                break;
            case 'v':
                g_state.draw_velocity_vector = 1;
                printf("Drawing velocity vector.\n");
                break;
            case 'w':
                g_state.write_frames = 1;
                printf("Writing frames to files in figs/viz directory.\n");
                break;
            case 'C':
                g_state.want_colorbar = 1;
                printf("Plotting colorbar.\n");
                break;
            case 'L':
                g_state.logscale = 1;
                printf("Using logscale.\n");
                break;
            default:
                break;
        }
        opt = getopt(argc, argv, g_optstring);
    }

    leftover_argv = argv + optind;
    leftover_argc = argc - optind;

    if (leftover_argc >= 1) {
        std::cout << "Using data file: " << leftover_argv[0] << std::endl;
        g_state.data_file = fopen(leftover_argv[0], "r");
        strncpy(g_state.loaded_file_path, leftover_argv[0], sizeof(g_state.loaded_file_path) / sizeof(g_state.loaded_file_path[0]));
        snprintf(g_state.wm_title, sizeof(g_state.wm_title) / sizeof(g_state.wm_title[0]), "%s", leftover_argv[0]);
        SDL_WM_SetCaption(g_state.wm_title, g_state.wm_title);
    }

    TXTReader t(leftover_argv[0], leftover_argv[0]);
//    CSVReader t(leftover_argv[0]);

    g_state.reader = &t;
//    auto XX = s->nextParticles();
//    std::cout << "read particle size: " << XX.size() << std::endl;
//    for (auto & k : XX) {
//        std::cout << k << std::endl;
//    }

    if (g_state.data_file == NULL) {
        std::cout << "No data file. Exiting." << std::endl;
        return 0;
    }

    if (colormap_file == NULL) {
        // load default colormap from memory (in viz_builtin_colormap.hpp)
        colormap_file = fmemopen((void *)viz_default_colormap_str,
            sizeof(viz_default_colormap_str) / sizeof(viz_default_colormap_str[0]),
            "r"
        );
        parse_colormap(&(g_state.colormap), colormap_file);
    }

    std::vector<std::string> fontlist;
    for (size_t i = 0; i < cfg_size(g_state.cfg, "fonts"); i++) {
        fontlist.push_back(std::string(cfg_getnstr(g_state.cfg, "fonts", i)));
    }

    init_opengl();
    init_ftgl(fontlist);

    /* Make background mesh */
    backgroundMesh = BackgroundMesh(bgsize, bgsize, 1.0, 1.0);

    heartbeat();

    SDL_Quit();

    return 0;
}
