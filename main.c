#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <X11/extensions/shape.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>

#include <GL/glx.h>
#include <cairo.h>

static Display* display;

static int dbuffered = False;

static int g_width, g_height;
static GLXContext ctx = 0;
static GLXWindow glxwin = 0;

static Pixmap back_pixmap = 0;
static GLXPixmap glx_pm = 0;

static GRand* rand = NULL;

static GtkWidget* ref = NULL, *top = NULL;


typedef void (*t_glx_bind)(Display *, GLXDrawable, int , const int *);
typedef void (*t_glx_release)(Display *, GLXDrawable, int);

t_glx_bind glXBindTexImageEXT = 0;
t_glx_release glXReleaseTexImageEXT = 0;

static const int pixmap_attribs[] = {
    GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
    GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGBA_EXT,
    None
};

static GLXFBConfig bestFbc;

static int setup_context()
{
    // Get a matching FB config
    static int fb_attribs[] = {
        GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
        GLX_RENDER_TYPE     , GLX_RGBA_BIT,
        GLX_RED_SIZE        , 8,
        GLX_GREEN_SIZE      , 8,
        GLX_BLUE_SIZE       , 8,
        GLX_ALPHA_SIZE      , 8,
        GLX_DOUBLEBUFFER    , False,
        None
    };
    fb_attribs[13] = dbuffered;

    int glx_major, glx_minor;
    // FBConfigs were added in GLX version 1.3.
    if ( !glXQueryVersion(display, &glx_major, &glx_minor) || 
            ((glx_major == 1) && (glx_minor < 3)) || (glx_major < 1)) {
        g_debug("Invalid GLX version");
        return -1;
    }

    int fbcount;
    GLXFBConfig* fbc = glXChooseFBConfig(display, DefaultScreen(display), 
            fb_attribs, &fbcount);
    if (!fbc) {
        g_debug( "Failed to retrieve a framebuffer config\n" );
        return -1;
    }
    g_debug("fbcount %d", fbcount);
    bestFbc = fbc[0];

    XFree( fbc );

    int gl3attr[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 1,
        /*GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,*/
        None
    };

    ctx = glXCreateNewContext(display, bestFbc, GLX_RGBA_TYPE, 0, True);
    if (ctx == 0) {
        g_debug("3.1 context failed");
        return -1;
    }
    XSync( display, False );

    if (ctx == 0) {
        g_debug("create gl context failed");
        return -1;
    }
    // Verifying that context is a direct context
    if (!glXIsDirect(display, ctx)) {
        g_debug( "Indirect GLX rendering context obtained\n" );
    } else {
        g_debug( "Direct GLX rendering context obtained\n" );
    }


    glXBindTexImageEXT = (t_glx_bind) glXGetProcAddress((const GLubyte *)"glXBindTexImageEXT");
    glXReleaseTexImageEXT = (t_glx_release) glXGetProcAddress((const GLubyte *)"glXReleaseTexImageEXT");

    return 0;
}

static gboolean on_draw(GtkWidget* w, cairo_t* cr, gpointer data)
{
    g_debug("%s", __func__);
    if (ctx == 0 || glxwin == 0 || glx_pm == 0) return TRUE;

    glXMakeContextCurrent(display, glxwin, glxwin, ctx);
    glViewport (0, 0, g_width, g_height);

    GdkWindow* gdkwin = gtk_widget_get_window(w);

    XWindowAttributes  gwa;
    XGetWindowAttributes(display, GDK_WINDOW_XID(gdkwin), &gwa);
    glViewport(0, 0, gwa.width, gwa.height);

    glClearColor(1.0, 1.0, 1.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    GLuint texture;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glXBindTexImageEXT(display, glx_pm, GLX_FRONT_EXT, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBegin(GL_QUADS);
        glTexCoord2f(0.0, 0.0); glVertex3f(-1.0,  1.0, 0.0);
        glTexCoord2f(1.0, 0.0); glVertex3f( 1.0,  1.0, 0.0);
        glTexCoord2f(1.0, 1.0); glVertex3f( 1.0, -1.0, 0.0);
        glTexCoord2f(0.0, 1.0); glVertex3f(-1.0, -1.0, 0.0);
    glEnd(); 

    if (dbuffered) glXSwapBuffers(display, glxwin);
    else glFlush(); 

    /*glXMakeCurrent(display, 0, 0);*/
    return TRUE;
}

static gboolean on_ref_configure(GtkWidget *widget, GdkEvent  *event,
               gpointer   user_data)
{
    g_debug("%s", __func__);
    GdkEventConfigure *gec = (GdkEventConfigure*)event;
    g_width = gec->width;
    g_height = gec->height;
    gtk_window_resize(GTK_WINDOW(top), g_width, g_height);

    if (!gtk_widget_get_mapped(ref)) return TRUE;

    if (back_pixmap) {
        glXDestroyPixmap(display, glx_pm);
        XFreePixmap(display, back_pixmap);

        GdkWindow* gdkwin = gtk_widget_get_window(ref);
        back_pixmap = XCompositeNameWindowPixmap(display, GDK_WINDOW_XID(gdkwin));
        glx_pm = glXCreatePixmap(display, bestFbc, back_pixmap, pixmap_attribs);
    }

    return TRUE;
}

static gboolean on_deleted(GtkWidget *widget, GdkEvent  *event,
               gpointer   user_data)
{
    g_debug("%s", __func__);
    gtk_main_quit();
    return FALSE;
}

static gboolean on_ref_mapped(GtkWidget *widget, GdkEvent  *event,
               gpointer   user_data)
{
    g_debug("%s", __func__);
    if (!back_pixmap) {
        g_assert(gtk_widget_is_visible(ref));
        GdkWindow* gdkwin = gtk_widget_get_window(ref);
        back_pixmap = XCompositeNameWindowPixmap(display, GDK_WINDOW_XID(gdkwin));
        glx_pm = glXCreatePixmap(display, bestFbc, back_pixmap, pixmap_attribs);
    }
    return FALSE;
}

static gboolean on_ref_draw(GtkWidget* widget, cairo_t* cr, gpointer data)
{
    double r = g_rand_double_range(rand, 0.0, 1.0);
    double g = g_rand_double_range(rand, 0.0, 1.0);
    double b = g_rand_double_range(rand, 0.0, 1.0);
    
    int x = g_rand_int_range(rand, 0, g_width);
    int y = g_rand_int_range(rand, 0, g_height);

    int w = g_rand_int_range(rand, g_width/2, g_width+1);
    int h = g_rand_int_range(rand, g_height/2, g_height+1);

    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    gtk_widget_queue_draw(top);
    return TRUE;
}

static gboolean on_timeout(gpointer data)
{
    gtk_widget_queue_draw(ref);
    return G_SOURCE_CONTINUE;
}

int main(int argc, char* argv[]) 
{
    gtk_init(&argc, &argv);

    display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

    rand = g_rand_new();

    g_width = 400;
    g_height = 300;

    if (setup_context() != 0) return -1;

    ref = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(ref), g_width, g_height);
    g_object_connect(ref,
            "signal::draw", on_ref_draw, NULL, 
            "signal::configure-event", on_ref_configure, NULL, 
            "signal::map-event", on_ref_mapped, NULL, 
            "signal::delete-event", on_deleted, NULL,
            NULL);

    gtk_widget_show_all(ref);


    top = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(top), g_width, g_height);
    gtk_widget_set_app_paintable(top, TRUE);
    gtk_widget_set_double_buffered(top, FALSE);

    g_object_connect(top,
            "signal::draw", on_draw, NULL, 
            "signal::delete-event", on_deleted, NULL,
            NULL);

    gtk_widget_show_all(top);
    GdkWindow* gdkwin = gtk_widget_get_window(top);
    glxwin = glXCreateWindow(display, bestFbc, GDK_WINDOW_XID(gdkwin), NULL);

    g_timeout_add(1000, on_timeout, NULL);
    gtk_main();

    if (glx_pm) {
        glXDestroyPixmap(display, glx_pm);
        XFreePixmap(display, back_pixmap);
    }
    glXDestroyContext(ctx);
    g_rand_free(rand);
}


