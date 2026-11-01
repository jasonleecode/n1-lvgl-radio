#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 Display *d=XOpenDisplay(NULL);if(!d)return 2;
 XSelectInput(d,DefaultRootWindow(d),SubstructureNotifyMask);XSync(d,False);
 pid_t child=fork();if(child==0){execl(argv[1],argv[1],"--demo","--exit-after","2",NULL);_exit(127);}if(child<0)return 2;
 int mapped=0,status=0;bool wrong=false,done=false;
 for(int i=0;i<5000;i++) {
  while(XPending(d)) {
   XEvent event;XNextEvent(d,&event);
   if(event.type==MapNotify) {
    XWindowAttributes attr;
    if(XGetWindowAttributes(d,event.xmap.window,&attr) && attr.width>100 && attr.height>100) {
     mapped++;printf("Mapped window: %dx%d\n",attr.width,attr.height);
     if(attr.width!=520 || attr.height!=400)wrong=true;
    }
   }
  }
  if(waitpid(child,&status,WNOHANG)==child){done=true;break;}
  struct timespec t={0,1000000};nanosleep(&t,NULL);
 }
 if(!done){kill(child,SIGTERM);waitpid(child,&status,0);}
 XCloseDisplay(d);
 if(!done || !mapped || wrong || !WIFEXITED(status) || WEXITSTATUS(status)!=0)return 1;
 puts("PASS: every visible startup window was already 520x400");return 0;
}
