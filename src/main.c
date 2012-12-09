/*

  Copyright (c) 2009, Nokia Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.  
    * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.  
    * Neither the name of Nokia nor the names of its contributors 
    may be used to endorse or promote products derived from this 
    software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
  COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */
/*
 * SmartSpace test
 *
 * main.c
 *
 * Copyright 2006 Nokia Corporation
 */
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <uuid/uuid.h>
#include "config.h"

#include <sib_object.h>
#include <whiteboard_log.h>
#include "nota_rm_handler.h"
#include "sib_handler.h"
#include "sib_maxfd.h"

static void refresh_cb(SibObject* context,
		       gpointer user_data);
static void shutdown_cb(SibObject* context,
			gpointer user_data);
static void healthcheck_cb(SibObject* context,
			   gpointer user_data);

static void register_sib_cb(SibObject* context,
			    SibObjectHandle *handle,
			    gchar *uri,
			    gpointer user_data);


static SibObject *create_control(NotaRMHandler *rm);

static void cleanup();
GMainLoop *mainloop = NULL;
KPListener *kplistener = NULL;
NotaRMHandler *rm_handler = NULL;
SibObject *control = NULL;
SIBMaxFd *maxfd = NULL;

void main_signal_handler(int sig)
{
  static volatile sig_atomic_t signalled = 0;
  if ( 1 == signalled )
    {
      signal(sig, SIG_DFL);
      raise(sig);
    }
  else
    {
      signalled = 1;
      cleanup();
      g_main_loop_quit(mainloop);
    }
}

int main(int argc, char *argv[])
{
  g_type_init();
  g_thread_init(NULL);
  /* Set signal handlers */
  signal(SIGINT, main_signal_handler);
  signal(SIGTERM, main_signal_handler);
   
  mainloop = g_main_loop_new(NULL, FALSE);
  g_main_loop_ref(mainloop);

  maxfd = sib_maxfd_new();
  rm_handler  = nota_rm_handler_new( maxfd );

  control = create_control(rm_handler);
  if(!control)
    {
      printf("\n\nCould not create control channel to sib-daemon, daemon running properly?\n");
      exit(1);
    }
  
  g_main_loop_run(mainloop);

  g_main_loop_unref(mainloop);
  
  printf("\n\nExiting normally\n\n");
  exit (0);
}

void cleanup()
{

  if( control)
    {
      g_object_unref( G_OBJECT( control ) );
      control = NULL;
    }
  
  if(kplistener)
    {
      kp_listener_destroy(kplistener);
      kplistener = NULL;
    }

  if(rm_handler )
    {
      nota_rm_handler_destroy( rm_handler);
      rm_handler = NULL;
    }
  if(maxfd)
    {
      sib_maxfd_destroy( maxfd );
      maxfd = NULL;
    }
}

SibObject *create_control(NotaRMHandler *rm)
{

  gchar tmp[37];
  uuid_t u1;
  
  whiteboard_log_debug_fb();

  uuid_generate(u1);

  uuid_unparse(u1, tmp);
  SibObject *control = 	SIB_OBJECT(sib_object_new(NULL,
						  tmp,
						  NULL,
						  "SIB Access",
						  "Not yet done"));
  g_return_val_if_fail(control!=NULL, NULL);
  
  g_signal_connect(G_OBJECT(control),
		   SIB_OBJECT_SIGNAL_REFRESH,
		   (GCallback) refresh_cb,
		   NULL);

  g_signal_connect(G_OBJECT(control),
		   SIB_OBJECT_SIGNAL_HEALTHCHECK,
		   (GCallback) healthcheck_cb,
		   NULL);
  
  g_signal_connect(G_OBJECT(control),
		   SIB_OBJECT_SIGNAL_SHUTDOWN,
		   (GCallback) shutdown_cb,
		   NULL);
  
  g_signal_connect(G_OBJECT(control),
		   SIB_OBJECT_SIGNAL_REGISTER_SIB,
		   (GCallback) register_sib_cb,
		   rm);
  
  return control;  
}

void refresh_cb(SibObject* context,
		       gpointer user_data)
{
}

void shutdown_cb(SibObject* context,
			gpointer user_data)
{
}

void healthcheck_cb(SibObject* context,
		    gpointer user_data)
{

}

void register_sib_cb(SibObject* context,
		     SibObjectHandle *handle,
		     gchar *uri,
		     gpointer user_data)
{
  NotaRMHandler *rm = (NotaRMHandler *)user_data;
  gint ret = -1;
  gint sid = -1;
  whiteboard_log_debug_fb();

  sid = nota_rm_handler_register_sib(rm, uri);
  
  if(sid<0)
    {
      whiteboard_log_debug("Registering sib: %s to RM failed\n", uri);
      ret = -1;
      sib_object_send_register_sib_return(handle, ret);
    }
  else
    {
      ret = 0;

      sib_object_send_register_sib_return(handle, ret);
      
      whiteboard_log_debug("Creating listener for SID: %d\n",sid);
      kplistener = kp_listener_new(sid, nota_rm_handler_get_maxfd(rm));
    }
  whiteboard_log_debug_fe();
}


