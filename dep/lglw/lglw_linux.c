/* ----
 * ---- file   : lglw_linux.c  (**stub**)
 * ---- author : bsp
 * ---- legal  : Distributed under terms of the MIT LICENSE (MIT).
 * ----
 * ---- Permission is hereby granted, free of charge, to any person obtaining a copy
 * ---- of this software and associated documentation files (the "Software"), to deal
 * ---- in the Software without restriction, including without limitation the rights
 * ---- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * ---- copies of the Software, and to permit persons to whom the Software is
 * ---- furnished to do so, subject to the following conditions:
 * ----
 * ---- The above copyright notice and this permission notice shall be included in
 * ---- all copies or substantial portions of the Software.
 * ----
 * ---- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * ---- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * ---- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * ---- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * ---- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * ---- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * ---- THE SOFTWARE.
 * ----
 * ---- info   : This is part of the "lglw" package.
 * ----
 * ---- created: 04Aug2018
 * ---- changed: 05Aug2018, 06Aug2018, 07Aug2018, 08Aug2018, 09Aug2018, 18Aug2018, 10Oct2018, 16Oct2018
 * ----
 * ----
 */

#include "lglw.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>

#include <GL/gl.h>
#include <GL/glx.h>

#define Dprintf if(0);else printf
// #define Dprintf if(1);else printf

// ---------------------------------------------------------------------------- macros and defines
#define LGLW(a) lglw_int_t *lglw = ((lglw_int_t*)(a))

#define LGLW_DEFAULT_HIDDEN_W  (800)
#define LGLW_DEFAULT_HIDDEN_H  (600)

#define LGLW_MOUSE_TOUCH_LMB_TIMEOUT   (250u)
#define LGLW_MOUSE_TOUCH_RMB_TIMEOUT   (500u)
#define LGLW_MOUSE_TOUCH_RMB_STATE_IDLE  (0u)
#define LGLW_MOUSE_TOUCH_RMB_STATE_LMB   (1u)
#define LGLW_MOUSE_TOUCH_RMB_STATE_WAIT  (2u)
#define LGLW_MOUSE_TOUCH_RMB_STATE_RMB   (3u)
#define LGLW_MOUSE_TOUCH_RMB_MOVE_THRESHOLD  (7u)

#define sABS(x) (((x)>0)?(x):-(x))


// ---------------------------------------------------------------------------- structs and typedefs
typedef struct lglw_int_s {
   void        *user_data;    // arbitrary user data
   Display     *xdsp;
   XVisualInfo *vi;
   Colormap     cmap;
   Window       parent_xwnd;  // created by host

   struct {
      lglw_vec2i_t size;
      Window       xwnd;
   } hidden;

   struct {
      lglw_vec2i_t size;
      Window       xwnd;
      lglw_bool_t  mapped;
      int32_t      swap_interval;
   } win;

   GLXContext   ctx;

   struct {
      GLXContext   ctx;
      GLXDrawable  drw;
   } prev;

   struct {
      uint32_t            kmod_state;  // See LGLW_KMOD_xxx
      lglw_keyboard_fxn_t cbk;
   } keyboard;

   struct {
      lglw_vec2i_t     p;  // last seen mouse position
      uint32_t         button_state;
      lglw_mouse_fxn_t cbk;
      struct {
         uint32_t         mode;
         lglw_vec2i_t     p;  // grab-start mouse position
         lglw_bool_t      b_queue_warp;
         lglw_vec2i_t     last_p;
      } grab;
      struct {
         lglw_bool_t      b_enable;
         lglw_bool_t      b_update_queued;
         lglw_bool_t      b_syn_rmb;
         uint32_t         syn_rmb_hold_state;  // see LGLW_MOUSE_TOUCH_RMB_STATE_xxx
         uint32_t         hold_start_ms;
         lglw_vec2i_t     hold_start_p;
      } touch;
   } mouse;

   struct {
      uint32_t         state;
      lglw_focus_fxn_t cbk;
   } focus;

   struct {
      lglw_bool_t      b_running;
      lglw_timer_fxn_t cbk;
   } timer;

   struct {
      lglw_dropfiles_fxn_t cbk;
   } dropfiles;

   struct {
      lglw_redraw_fxn_t cbk;
   } redraw;

} lglw_int_t;


// ---------------------------------------------------------------------------- module fxn fwd decls
static lglw_bool_t loc_create_hidden_window (lglw_int_t *lglw, int32_t _w, int32_t _h);
static void loc_destroy_hidden_window(lglw_int_t *lglw);

static void loc_key_hook(lglw_int_t *lglw);
static void loc_key_unhook(lglw_int_t *lglw);
static lglw_bool_t loc_handle_key (lglw_int_t *lglw, lglw_bool_t _bPressed, uint32_t _vkey);
// static lglw_bool_t loc_touchkeyboard_get_rect (RECT *rect);
// static lglw_bool_t loc_touchkeyboard_is_visible (void);
extern lglw_bool_t lglw_int_touchkeyboard_toggle (void);

static void loc_handle_mouseleave (lglw_int_t *lglw);
static void loc_handle_mouseenter (lglw_int_t *lglw);
static void loc_handle_mousebutton (lglw_int_t *lglw, lglw_bool_t _bPressed, uint32_t _button);
static void loc_handle_mousemotion (lglw_int_t *lglw);
static void loc_handle_queued_mouse_warp (lglw_int_t *lglw);
static void loc_touchinput_update (lglw_int_t *lglw);

static void loc_enable_dropfiles (lglw_int_t *lglw, lglw_bool_t _bEnable);


// ---------------------------------------------------------------------------- module vars
static lglw_int_t *khook_lglw = NULL;  // currently key-hooked lglw instance (one at a time)


// TODO: remove and/or improve debug logging for a debug build
// ---------------------------------------------------------------------------- lglw_log
static FILE *logfile;

void lglw_log(const char *logData, ...) {
   fprintf(logfile, logData);
   fflush(logfile);
}


// TODO: remove, or maybe not in some specific use cases
// ---------------------------------------------------------------------------- xerror_log
static int xerror_handler(Display *display, XErrorEvent *error) {
   char error_text[1024];
   XGetErrorText(display, error->error_code, error_text, 1024);
   lglw_log("XERROR (%d): %s, %d, %d\n", error->error_code, error_text, error->request_code, error->minor_code);
   return 0;
}


// ---------------------------------------------------------------------------- lglw_init
lglw_t lglw_init(int32_t _w, int32_t _h) {
   lglw_int_t *lglw = malloc(sizeof(lglw_int_t));

   // TODO: remove/improve
   logfile = fopen("/tmp/lglw_log.txt", "w");
   XSetErrorHandler(xerror_handler);

   if(NULL != lglw)
   {
      memset(lglw, 0, sizeof(lglw_int_t));

      lglw_log("lglw:lglw_init: 1\n");
      if(_w <= 16)
         _w = LGLW_DEFAULT_HIDDEN_W;

      if(_h <= 16)
         _h = LGLW_DEFAULT_HIDDEN_H;

      lglw_log("lglw:lglw_init: 2\n");
      if(!loc_create_hidden_window(lglw, _w, _h))
      {
         free(lglw);
         lglw = NULL;
      }
      lglw_log("lglw:lglw_init: 3\n");
   }

   lglw_log("lglw:lglw_init: EXIT\n");

   return lglw;
}


// ---------------------------------------------------------------------------- lglw_exit
void lglw_exit(lglw_t _lglw) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw_log("lglw:lglw_exit: 1\n");

      loc_destroy_hidden_window(lglw);

      lglw_log("lglw:lglw_exit: 2\n");

      fclose(logfile);
      free(lglw);
   }
}


// ---------------------------------------------------------------------------- lglw_userdata_set
void lglw_userdata_set(lglw_t _lglw, void *_userData) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw_log("lglw:lglw_userdata_set: 1\n");
      lglw->user_data = _userData;
   }
}

// ---------------------------------------------------------------------------- lglw_userdata_get
void *lglw_userdata_get(lglw_t _lglw) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw_log("lglw:lglw_userdata_get: 1\n");
      return lglw->user_data;
   }

   return NULL;
}


// ---------------------------------------------------------------------------- loc_create_hidden_window
static lglw_bool_t loc_create_hidden_window(lglw_int_t *lglw, int32_t _w, int32_t _h) {

   // TODO: compare to 'WindowClass' from Windows implementation

   lglw_log("lglw:loc_create_hidden_window: 1\n");
   XSetWindowAttributes swa;
   int attrib[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 24, None };
   int screen;

   lglw_log("lglw:loc_create_hidden_window: 2\n");
   lglw->xdsp = XOpenDisplay(NULL);
   screen = DefaultScreen(lglw->xdsp);

   lglw_log("lglw:loc_create_hidden_window: 3\n");
   lglw->vi = glXChooseVisual(lglw->xdsp, screen, attrib);

   lglw_log("lglw:loc_create_hidden_window: 4\n");
   if(NULL == lglw->vi)
   {
      lglw_log("[---] lglw: failed to find GLX Visual for hidden window\n");
      return LGLW_FALSE;
   }

   lglw_log("lglw:loc_create_hidden_window: 5\n");
   lglw->ctx = glXCreateContext(lglw->xdsp, lglw->vi, None, True);

   lglw_log("lglw:loc_create_hidden_window: 6\n");
   if(NULL == lglw->ctx)
   {
      lglw_log("[---] lglw: failed to create GLX Context for hidden window\n");
      return LGLW_FALSE;
   }

   lglw_log("lglw:loc_create_hidden_window: 7\n");
   lglw->cmap = XCreateColormap(lglw->xdsp, RootWindow(lglw->xdsp, lglw->vi->screen),
                                lglw->vi->visual, AllocNone);

   lglw_log("lglw:loc_create_hidden_window: 8\n");
   swa.border_pixel = 0;
   swa.colormap = lglw->cmap;
   lglw->hidden.xwnd = XCreateWindow(lglw->xdsp, DefaultRootWindow(lglw->xdsp),
      0, 0, LGLW_DEFAULT_HIDDEN_W, LGLW_DEFAULT_HIDDEN_H, 0, CopyFromParent, InputOutput,
      lglw->vi->visual, CWBorderPixel | CWColormap, &swa);

   lglw_log("lglw:loc_create_hidden_window: 9\n");
   XSetStandardProperties(lglw->xdsp, lglw->hidden.xwnd, "LGLW_hidden", "LGLW_hidden", None, NULL, 0, NULL);
   XSync(lglw->xdsp, False);

   lglw_log("lglw:loc_create_hidden_window: EXIT\n");
   lglw->hidden.size.x = _w;
   lglw->hidden.size.y = _h;

   return LGLW_TRUE;
}


// ---------------------------------------------------------------------------- loc_destroy_hidden_window
static void loc_destroy_hidden_window(lglw_int_t *lglw) {
   lglw_log("lglw:loc_destroy_hidden_window: 1\n");
   if(NULL != lglw->xdsp && NULL != lglw->ctx)
   {
      glXMakeCurrent(lglw->xdsp, None, NULL);
      glXDestroyContext(lglw->xdsp, lglw->ctx);
   }
   lglw_log("lglw:loc_destroy_hidden_window: 2\n");
   if(NULL != lglw->xdsp && 0 != lglw->hidden.xwnd) XDestroyWindow(lglw->xdsp, lglw->hidden.xwnd);
   lglw_log("lglw:loc_destroy_hidden_window: 3\n");
   if(NULL != lglw->xdsp && 0 != lglw->cmap) XFreeColormap(lglw->xdsp, lglw->cmap);
   lglw_log("lglw:loc_destroy_hidden_window: 4\n");
   if(NULL != lglw->vi) XFree(lglw->vi);

   lglw_log("lglw:loc_destroy_hidden_window: 5\n");
   XSync(lglw->xdsp, False);
   if(NULL != lglw->xdsp) XCloseDisplay(lglw->xdsp);
}


// ---------------------------------------------------------------------------- loc_setEventProc
// https://www.kvraudio.com/forum/viewtopic.php?t=387924
// https://github.com/Ardour/ardour/blob/master/gtk2_ardour/linux_vst_gui_support.cc
// https://discourse.ardour.org/t/overtonedsp-plugins/90115/22
// https://github.com/amsynth/amsynth/blob/4a87798e650c6d71d70274a961c9b8d98fc6da7e/src/amsynth_vst.cpp
// https://github.com/rsenn/eXT2/blob/7f00a09561ded8175ffed2f4912dad74e466a1c7/vstplugins/vstgui/vstgui.cpp
// https://github.com/COx2/DistortionFilter/blob/c6a34fb56b503a6e95bf0975e00f438bbf4ff52a/juce/modules/juce_audio_processors/format_types/juce_VSTPluginFormat.cpp

// Very simple function to test _XEventProc is properly called
void loc_eventProc(void *xevent) {
   // lglw_log("XEventProc");
   printf("XEventProc\n");
}

// Pulled from the Renoise 64-bit callback example
// Unsure what data was supposed to be, but swapping it to a function name did not work
// This does nothing, no event proc found
// TODO: 32-bit support
// void loc_setEventProc (Display *display, Window window) {
//    size_t data = (size_t)loc_eventProc;
//    long temp[2];

//    // Split the 64 bit pointer into a little-endian long array
//    temp[0] = (long)(data & 0xffffffffUL);
//    temp[1] = (long)(data >> 32L);

//    Atom atom = XInternAtom(display, "_XEventProc", False);
//    XChangeProperty(display, window, atom, atom, 32,
//       PropModeReplace, (unsigned char*)temp, 2);
// }

// Pulled from the eXT2 example
// TODO: 32-bit support
void loc_setEventProc (Display *display, Window window) {
   void* data = (void*)&loc_eventProc; // swapped the function name here

   Atom atom = XInternAtom(display, "_XEventProc", False);
   XChangeProperty(display, window, atom, atom, 32,
      PropModeReplace, (unsigned char*)&data, 1); // 1 instead of 2 will crash Ardour, 2 will not do anything
}

// Pulled from the amsynth example
// Simply swapped out the function names, crashes Ardour in the same was as the others
// void loc_setEventProc (Display *display, Window window) {
// #ifdef BUILD_64
//    //
//    // JUCE calls XGetWindowProperty with long_length = 1 which means it only fetches the lower 32 bits of the address.
//    // Therefore we need to ensure we return an address in the lower 32-bits of address space.
//    //

//    // based on mach_override
//    static const unsigned char kJumpInstructions[] = {
//          0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
//          0x00, 0x00, 0x00, 0x00,
//          0x00, 0x00, 0x00, 0x00
//    };
//    static const int kJumpAddress = 6;

//    static char *ptr;
//    if (!ptr) {
//       ptr = (char *)mmap(0,
//                      PAGE_SIZE,
//                      PROT_READ | PROT_WRITE | PROT_EXEC,
//                      MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT,
//                      0, 0);
//       if (ptr == MAP_FAILED) {
//          perror("mmap");
//          ptr = 0;
//          return;
//       } else {
//          memcpy(ptr, kJumpInstructions, sizeof(kJumpInstructions));
//          *((uint64_t *)(ptr + kJumpAddress)) = (uint64_t)(&loc_eventProc);
//          msync(ptr, sizeof(kJumpInstructions), MS_INVALIDATE);
//       }
//    }

//    long temp[2] = {(long)ptr, 0};
//    Atom atom = XInternAtom(display, "_XEventProc", False);
//    XChangeProperty(display, window, atom, atom, 32, PropModeReplace, (unsigned char *)temp, 2);
// #else
//    long temp[1] = {(long)(void *)(&loc_eventProc)};
//    Atom atom = XInternAtom(display, "_XEventProc", False);
//    XChangeProperty(display, window, atom, atom, 32, PropModeReplace, (unsigned char *)temp, 1);
// #endif
// }


// ---------------------------------------------------------------------------- lglw_window_open
lglw_bool_t lglw_window_open (lglw_t _lglw, void *_parentHWNDOrNull, int32_t _x, int32_t _y, int32_t _w, int32_t _h) {
   lglw_bool_t r = LGLW_FALSE;
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw_log("lglw:lglw_window_open: 1, %p, %i\n", (Window)_parentHWNDOrNull, (Window)_parentHWNDOrNull);
      lglw->parent_xwnd = (0 == _parentHWNDOrNull) ? DefaultRootWindow(lglw->xdsp) : (Window)_parentHWNDOrNull;

      lglw_log("lglw:lglw_window_open: 2\n");
      if(_w <= 16)
         _w = lglw->hidden.size.x;

      lglw_log("lglw:lglw_window_open: 3\n");
      if(_h <= 16)
         _h = lglw->hidden.size.y;

      // TODO: compare to 'WindowClass' from Windows implementation

      lglw_log("lglw:lglw_window_open: 4\n");
      XSetWindowAttributes swa;
      XEvent event;
      XSync(lglw->xdsp, False);

      lglw_log("lglw:lglw_window_open: 5\n");
      swa.border_pixel = 0;
      swa.colormap = lglw->cmap;
      swa.event_mask = EnterWindowMask | LeaveWindowMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask;
      lglw->win.xwnd = XCreateWindow(lglw->xdsp, DefaultRootWindow(lglw->xdsp),
         0, 0, _w, _h, 0, CopyFromParent, InputOutput,
         lglw->vi->visual, CWBorderPixel | CWColormap | CWEventMask, &swa);

      lglw_log("lglw:lglw_window_open: 6\n");
      XSetStandardProperties(lglw->xdsp, lglw->win.xwnd, "LGLW", "LGLW", None, NULL, 0, NULL);

      // Setup the event proc now, on the parent window as well just for the debug host
      // It was simpler to do this than check in the debug host for the reparent event
      lglw_log("lglw:lglw_window_open: 7\n");
      loc_setEventProc(lglw->xdsp, lglw->win.xwnd);
      loc_setEventProc(lglw->xdsp, lglw->parent_xwnd);

      // Some hosts only check and store the callback when the Window is reparented
      // Since creating the Window with a Parent may or may not do that, but the callback is not set,
      // ... it's created as a root window, the callback is set, and then it's reparented
      if (0 != _parentHWNDOrNull)
      {
         lglw_log("lglw:lglw_window_open: 8\n");
         XReparentWindow(lglw->xdsp, lglw->win.xwnd, lglw->parent_xwnd, 0, 0);
      }

      lglw_log("lglw:lglw_window_open: 9\n");
      XMapRaised(lglw->xdsp, lglw->win.xwnd);
      XSync(lglw->xdsp, False);
      lglw->win.mapped = LGLW_TRUE;

      lglw_log("lglw:lglw_window_open: 10\n");
      lglw->win.size.x = _w;
      lglw->win.size.y = _h;

      lglw_log("lglw:lglw_window_open: 11\n");
      loc_enable_dropfiles(lglw, (NULL != lglw->dropfiles.cbk));

      lglw_log("lglw:lglw_window_open: EXIT\n");
   }
   return r;
}


// ---------------------------------------------------------------------------- lglw_window_resize
lglw_bool_t lglw_window_resize (lglw_t _lglw, int32_t _w, int32_t _h) {
   lglw_bool_t r = LGLW_FALSE;
   LGLW(_lglw);

   if(NULL != lglw)
   {
      if(0 != lglw->win.xwnd)
      {
         lglw_log("lglw:lglw_window_resize: 1\n");
         r = LGLW_TRUE;

         lglw_log("lglw:lglw_window_resize: 2\n");
         XResizeWindow(lglw->xdsp, lglw->win.xwnd, _w, _h);
         XRaiseWindow(lglw->xdsp, lglw->win.xwnd);

         lglw_log("lglw:lglw_window_resize: 3\n");
         int deltaW = _w - lglw->win.size.x;
         int deltaH = _h - lglw->win.size.y;

         lglw_log("lglw:lglw_window_resize: 4\n");
         lglw->win.size.x = _w;
         lglw->win.size.y = _h;

         lglw_log("lglw:lglw_window_resize: 5\n");
         Window root, parent, *children = NULL;
         unsigned int num_children;

         lglw_log("lglw:lglw_window_resize: 6\n");
         if(!XQueryTree(lglw->xdsp, lglw->win.xwnd, &root, &parent, &children, &num_children))
            return r;

         lglw_log("lglw:lglw_window_resize: 7\n");
         if(children)
            XFree((char *)children);

         lglw_log("lglw:lglw_window_resize: 8\n");
         // Resize parent window (if any)
         if(0 != parent)
         {
            lglw_log("lglw:lglw_window_resize: 8.1\n");
            int x, y;
            unsigned int width, height;
            unsigned int border_width;
            unsigned int depth;

            lglw_log("lglw:lglw_window_resize: 8.2\n");
            if(!XGetGeometry(lglw->xdsp, lglw->win.xwnd, &root, &x, &y, &width, &height, &border_width, &depth))
               return r;

            lglw_log("lglw:lglw_window_resize: 8.3\n");
            XResizeWindow(lglw->xdsp, parent, width + deltaW, height + deltaH);
         }
         lglw_log("lglw:lglw_window_resize: EXIT\n");
      }
   }

   return r;
}


// ---------------------------------------------------------------------------- lglw_window_close
void lglw_window_close (lglw_t _lglw) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      if(0 != lglw->win.xwnd)
      {
         lglw_log("lglw:lglw_window_close: 1\n");
         lglw_timer_stop(_lglw);

         lglw_log("lglw:lglw_window_close: 2\n");
         loc_key_unhook(lglw);

         lglw_log("lglw:lglw_window_close: 3\n");
         glXMakeCurrent(lglw->xdsp, None, NULL);

         lglw_log("lglw:lglw_window_close: 4\n");
         XDestroyWindow(lglw->xdsp, lglw->win.xwnd);
         XSync(lglw->xdsp, False);
         lglw->win.xwnd = 0;
         lglw->win.mapped = LGLW_FALSE;
      }
   }
   lglw_log("lglw:lglw_window_close: EXIT\n");
}


// ---------------------------------------------------------------------------- lglw_window_show
void lglw_window_show(lglw_t _lglw) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw_log("lglw:lglw_window_show: 1\n");
      XMapRaised(lglw->xdsp, lglw->win.xwnd);

      lglw->win.mapped = LGLW_TRUE;
   }
   lglw_log("lglw:lglw_window_show: EXIT\n");
}


// ---------------------------------------------------------------------------- lglw_window_hide
void lglw_window_hide(lglw_t _lglw) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw_log("lglw:lglw_window_hide: 1\n");
      XUnmapWindow(lglw->xdsp, lglw->win.xwnd);

      lglw->win.mapped = LGLW_FALSE;
   }
   lglw_log("lglw:lglw_window_hide: EXIT\n");
}


// ---------------------------------------------------------------------------- lglw_window_is_visible
lglw_bool_t lglw_window_is_visible(lglw_t _lglw) {
   lglw_bool_t r = LGLW_FALSE;
   LGLW(_lglw);

   // lglw_log("lglw:lglw_window_is_visible: 1\n");
   if(NULL != lglw && 0 != lglw->win.xwnd)
   {
      // lglw_log("lglw:lglw_window_is_visible: 2\n");
      r = lglw->win.mapped;
   }

   // lglw_log("lglw:lglw_window_is_visible: EXIT\n");
   return r;
}


// ---------------------------------------------------------------------------- lglw_window_size_get
void lglw_window_size_get(lglw_t _lglw, int32_t *_retX, int32_t *_retY) {
   LGLW(_lglw);

   lglw_log("lglw:lglw_window_size_get: 1\n");
   if(NULL != lglw)
   {
      if(0 != lglw->win.xwnd)
      {
         if(NULL != _retX)
            *_retX = lglw->win.size.x;

         if(NULL != _retY)
            *_retY = lglw->win.size.y;
      }
   }
   lglw_log("lglw:lglw_window_size_get: EXIT\n");
}


// ---------------------------------------------------------------------------- lglw_redraw
void lglw_redraw(lglw_t _lglw) {
   LGLW(_lglw);

   // (todo) implement me

   if(NULL != lglw)
   {
      if(0 != lglw->win.xwnd)
      {
         // TODO Event Loop
         lglw_log("lglw:lglw_redraw: 1\n");
         XClearArea(lglw->xdsp, lglw->win.xwnd, 0, 0, 1, 1, True); // clear tiny area for exposing
         XFlush(lglw->xdsp);
      }
   }
}


// ---------------------------------------------------------------------------- lglw_redraw_callback_set
void lglw_redraw_callback_set(lglw_t _lglw, lglw_redraw_fxn_t _cbk) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw->redraw.cbk = _cbk;
   }
}


// ---------------------------------------------------------------------------- lglw_glcontext_push
void lglw_glcontext_push(lglw_t _lglw) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw->prev.drw = glXGetCurrentDrawable();
      lglw->prev.ctx = glXGetCurrentContext();

      // lglw_log("lglw:lglw_glcontext_push: win.xwnd=%p hidden.xwnd=%p ctx=%p\n",
      //    lglw->win.xwnd, lglw->hidden.xwnd, lglw->ctx);
      if(!glXMakeCurrent(lglw->xdsp, (0 == lglw->win.xwnd) ? lglw->hidden.xwnd : lglw->win.xwnd, lglw->ctx))
      {
         lglw_log("[---] lglw_glcontext_push: glXMakeCurrent() failed. win.xwnd=%p hidden.xwnd=%p ctx=%p glGetError()=%d\n", lglw->win.xwnd, lglw->hidden.xwnd, lglw->ctx, glGetError());
      }
   }
}


// ---------------------------------------------------------------------------- lglw_glcontext_pop
void lglw_glcontext_pop(lglw_t _lglw) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      // lglw_log("lglw:lglw_glcontext_pop: prev.drw=%p prev.ctx=%p\n",
      //    lglw->prev.drw, lglw->prev.ctx);
      if(!glXMakeCurrent(lglw->xdsp, lglw->prev.drw, lglw->prev.ctx))
      {
         lglw_log("[---] lglw_glcontext_pop: glXMakeCurrent() failed. prev.drw=%p ctx=%p glGetError()=%d\n", lglw->prev.drw, lglw->prev.ctx, glGetError());
      }
   }
}


// ---------------------------------------------------------------------------- lglw_swap_buffers
void lglw_swap_buffers(lglw_t _lglw) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      if(0 != lglw->win.xwnd)
      {
         // lglw_log("lglw:lglw_swap_buffers: 1\n");
         glXSwapBuffers(lglw->xdsp, lglw->win.xwnd);
      }
   }
}


// ---------------------------------------------------------------------------- lglw_swap_interval_set
typedef void (APIENTRY *PFNWGLEXTSWAPINTERVALPROC) (int);
void lglw_swap_interval_set(lglw_t _lglw, int32_t _ival) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw_log("lglw:lglw_swap_interval_set: 1\n");
      PFNWGLEXTSWAPINTERVALPROC glXSwapIntervalEXT;
      glXSwapIntervalEXT = (PFNWGLEXTSWAPINTERVALPROC) glXGetProcAddress("glXSwapIntervalEXT");
      if(NULL != glXSwapIntervalEXT)
      {
         lglw_log("lglw:lglw_swap_interval_set: 2\n");
         glXSwapIntervalEXT(_ival);
         lglw->win.swap_interval = _ival;
      }
   }
}


// ---------------------------------------------------------------------------- lglw_swap_interval_get
int32_t lglw_swap_interval_get(lglw_t _lglw) {
   LGLW(_lglw);
   int32_t r = 0;

   if(NULL != lglw)
   {
      r = lglw->win.swap_interval;
   }

   return r;
}


// ---------------------------------------------------------------------------- loc_key_hook
static void loc_key_hook(lglw_int_t *lglw) {
   loc_key_unhook(lglw);

   // (todo) implement me

   khook_lglw = lglw;
}


// ---------------------------------------------------------------------------- loc_key_unhook
static void loc_key_unhook(lglw_int_t *lglw) {

   // (todo) implement me
   if(khook_lglw == lglw)
      khook_lglw = NULL;
}


// ---------------------------------------------------------------------------- loc_handle_mouseleave
static void loc_handle_mouseleave(lglw_int_t *lglw) {
   loc_key_unhook(lglw);

   lglw->focus.state &= ~LGLW_FOCUS_MOUSE;

   if(NULL != lglw->focus.cbk)
   {
      lglw->focus.cbk(lglw, lglw->focus.state, LGLW_FOCUS_MOUSE);
   }

   lglw_log("xxx lglw:loc_handle_mouseleave: LEAVE\n");
}


// ---------------------------------------------------------------------------- loc_handle_mouseenter
static void loc_handle_mouseenter(lglw_int_t *lglw) {
   
   loc_key_hook(lglw);

   lglw->focus.state |= LGLW_FOCUS_MOUSE;

   if(NULL != lglw->focus.cbk)
   {
      lglw->focus.cbk(lglw, lglw->focus.state, LGLW_FOCUS_MOUSE);
   }

   lglw_log("xxx lglw:loc_handle_mouseenter: LEAVE\n");
}


// ---------------------------------------------------------------------------- loc_handle_mousebutton
static void loc_handle_mousebutton(lglw_int_t *lglw, lglw_bool_t _bPressed, uint32_t _button) {
   if(_bPressed)
      lglw->mouse.button_state |= _button;
   else
      lglw->mouse.button_state &= ~_button;

   if(NULL != lglw->mouse.cbk)
   {
      lglw->mouse.cbk(lglw, lglw->mouse.p.x, lglw->mouse.p.y, lglw->mouse.button_state, _button);
   }
}


// ---------------------------------------------------------------------------- loc_handle_mousemotion
static void loc_handle_mousemotion(lglw_int_t *lglw) {

   if(NULL != lglw->mouse.cbk)
   {
      lglw->mouse.cbk(lglw, lglw->mouse.p.x, lglw->mouse.p.y, lglw->mouse.button_state, 0u/*changedbuttonstate*/);
   }
}


// ---------------------------------------------------------------------------- lglw_mouse_callback_set
void lglw_mouse_callback_set(lglw_t _lglw, lglw_mouse_fxn_t _cbk) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw->mouse.cbk = _cbk;
   }
}


// ---------------------------------------------------------------------------- lglw_mouse_callback_set
void lglw_focus_callback_set(lglw_t _lglw, lglw_focus_fxn_t _cbk) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw->focus.cbk = _cbk;
   }
}


// ---------------------------------------------------------------------------- loc_handle_key
static lglw_bool_t loc_handle_key(lglw_int_t *lglw, lglw_bool_t _bPressed, uint32_t _vkey) {
   lglw_bool_t r = LGLW_FALSE;

   if(NULL != lglw->keyboard.cbk)
   {
      r = lglw->keyboard.cbk(lglw, _vkey, lglw->keyboard.kmod_state, _bPressed);
   }

   return r;
}


// ---------------------------------------------------------------------------- lglw_keyboard_callback_set
void lglw_keyboard_callback_set(lglw_t _lglw, lglw_keyboard_fxn_t _cbk) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw->keyboard.cbk = _cbk;
   }
}


// ---------------------------------------------------------------------------- lglw_keyboard_get_modifiers
uint32_t lglw_keyboard_get_modifiers(lglw_t _lglw) {
   uint32_t r = 0u;
   LGLW(_lglw);

   if(NULL != lglw)
   {
      r = lglw->keyboard.kmod_state;
   }

   return r;
}


// ---------------------------------------------------------------------------- lglw_touchkeyboard_show
void lglw_touchkeyboard_show(lglw_t _lglw, lglw_bool_t _bEnable) {
   LGLW(_lglw);

   // (todo) implement me

   if(NULL != lglw)
   {
   }
}


// ---------------------------------------------------------------------------- lglw_mouse_get_buttons
uint32_t lglw_mouse_get_buttons(lglw_t _lglw) {
   uint32_t r = 0u;
   LGLW(_lglw);

   if(NULL != lglw)
   {
      r = lglw->mouse.button_state;
   }

   return r;
}


// ---------------------------------------------------------------------------- lglw_mouse_grab
void lglw_mouse_grab(lglw_t _lglw, uint32_t _grabMode) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      // (todo) implement me
      // if(NULL != lglw->win.hwnd)
      {
         if(!lglw->mouse.touch.b_enable)
         {
            if(lglw->mouse.grab.mode != _grabMode)
            {
               lglw_mouse_ungrab(_lglw);
            }

            switch(_grabMode)
            {
               default:
               case LGLW_MOUSE_GRAB_NONE:
                  break;

               case LGLW_MOUSE_GRAB_CAPTURE:
                  // (todo) implement me
                  // (void)SetCapture(lglw->win.hwnd);
                  lglw->mouse.grab.mode = _grabMode;
                  break;

               case LGLW_MOUSE_GRAB_WARP:
                  // (todo) implement me
                  // (void)SetCapture(lglw->win.hwnd);
                  lglw_mouse_cursor_show(_lglw, LGLW_FALSE);
                  lglw->mouse.grab.p = lglw->mouse.p;
                  lglw->mouse.grab.last_p = lglw->mouse.p;
                  lglw->mouse.grab.mode = _grabMode;
                  break;
            }
         }
      }
   }
}


// ---------------------------------------------------------------------------- lglw_mouse_ungrab
void lglw_mouse_ungrab(lglw_t _lglw) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      // (todo) implement me
      // if(NULL != lglw->win.hwnd)
      {
         if(!lglw->mouse.touch.b_enable)
         {
            switch(lglw->mouse.grab.mode)
            {
               default:
               case LGLW_MOUSE_GRAB_NONE:
                  break;

               case LGLW_MOUSE_GRAB_CAPTURE:
                  // (todo) implement me
                  // (void)ReleaseCapture();
                  lglw->mouse.grab.mode = LGLW_MOUSE_GRAB_NONE;
                  break;

               case LGLW_MOUSE_GRAB_WARP:
                  // (todo) implement me
                  // (void)ReleaseCapture();
                  lglw->mouse.grab.mode = LGLW_MOUSE_GRAB_NONE;
                  lglw->mouse.grab.b_queue_warp = LGLW_TRUE;
                  lglw_mouse_cursor_show(_lglw, LGLW_TRUE);
                  break;
            }
         }
      }
   }
}


// ---------------------------------------------------------------------------- lglw_mouse_warp
void lglw_mouse_warp(lglw_t _lglw, int32_t _x, int32_t _y) {
   LGLW(_lglw);

   // (todo) implement me

   if(NULL != lglw)
   {
   }
}


// ---------------------------------------------------------------------------- loc_handle_queued_mouse_warp
static void loc_handle_queued_mouse_warp(lglw_int_t *lglw) {
   if(lglw->mouse.grab.b_queue_warp)
   {
      lglw->mouse.grab.b_queue_warp = LGLW_FALSE;
      lglw_mouse_warp(lglw, lglw->mouse.grab.p.x, lglw->mouse.grab.p.y);
      lglw->mouse.grab.last_p = lglw->mouse.grab.p;
   }
}


// ---------------------------------------------------------------------------- lglw_mouse_cursor_show
void lglw_mouse_cursor_show (lglw_t _lglw, lglw_bool_t _bShow) {
   LGLW(_lglw);

   // (todo) implement me

   if(NULL != lglw)
   {
   }
}


// ---------------------------------------------------------------------------- lglw_timer_start
void lglw_timer_start(lglw_t _lglw, uint32_t _millisec) {
   LGLW(_lglw);

   // (todo) implement me

   if(NULL != lglw)
   {
   }
}


// ---------------------------------------------------------------------------- lglw_timer_stop
void lglw_timer_stop(lglw_t _lglw) {
   LGLW(_lglw);

   // (todo) implement me

   if(NULL != lglw)
   {
   }
}


// ---------------------------------------------------------------------------- lglw_timer_callback_set
void lglw_timer_callback_set(lglw_t _lglw, lglw_timer_fxn_t _cbk) {
   LGLW(_lglw);

   if(NULL != lglw)
   {
      lglw->timer.cbk = _cbk;
   }
}


// ---------------------------------------------------------------------------- loc_enable_dropfiles
static void loc_enable_dropfiles(lglw_int_t *lglw, lglw_bool_t _bEnable) {

   // (todo) implement me
}


// ---------------------------------------------------------------------------- lglw_dropfiles_callback_set
void lglw_dropfiles_callback_set(lglw_t _lglw, lglw_dropfiles_fxn_t _cbk) {
   LGLW(_lglw);

   if(NULL != _lglw)
   {
      lglw->dropfiles.cbk = _cbk;

      loc_enable_dropfiles(lglw, (NULL != _cbk));
   }
}


// ---------------------------------------------------------------------------- loc_touchinput_update
static void loc_touchinput_update(lglw_int_t *lglw) {

   // (todo) implement me
}


// ---------------------------------------------------------------------------- lglw_touchinput_set
void lglw_touchinput_set(lglw_t _lglw, lglw_bool_t _bEnable) {
   LGLW(_lglw);

   if(NULL != _lglw)
   {
      lglw->mouse.touch.b_enable = _bEnable;
      lglw->mouse.touch.b_update_queued = LGLW_TRUE;
   }
}


// ---------------------------------------------------------------------------- lglw_touchinput_get
lglw_bool_t lglw_touchinput_get(lglw_t _lglw) {
   lglw_bool_t r = LGLW_FALSE;
   LGLW(_lglw);

   if(NULL != _lglw)
   {
      r = lglw->mouse.touch.b_enable;
   }

   return r;
}


// ---------------------------------------------------------------------------- lglw_clipboard_text_set
void lglw_clipboard_text_set(lglw_t _lglw, const uint32_t _numChars, const char *_text) {
   LGLW(_lglw);
   (void)_numChars;

   // (todo) implement me

   if(NULL != _text)
   {
   }
}


// ---------------------------------------------------------------------------- lglw_clipboard_text_get
void lglw_clipboard_text_get(lglw_t _lglw, uint32_t _maxChars, uint32_t *_retNumChars, char *_retText) {
   LGLW(_lglw);

   if(NULL != _retNumChars)
      *_retNumChars = 0u;

   if(NULL != _retText)
      *_retText = 0;

   if(_maxChars > 0u)
   {
      if(NULL != _lglw)
      {
         // (todo) implement me
      }
   }
}