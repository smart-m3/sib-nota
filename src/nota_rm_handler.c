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
#ifdef HAVE_CONFIG_H
#include"config.h"
#endif

#include <glib.h>

#include <h_in/h_bsdapi.h>

#include <whiteboard_log.h>

#if USE_RM==1
//#include <stubgen/resourcemng.h>
#define SID_RM 1

#include "NoTA_System_ResourceMng_R01_01_user.h"
#endif

#include "nota_rm_handler.h"
#include "sib_maxfd.h"

struct _NotaRMHandler
{
  int fd;
  struct context_pointer *cp;

  int regConfReceived;
  int deregConfReceived;
  gchar *dereg_cert;
  gchar *dereg_act;
  
  //  GCond *cond; 
  //GMutex *lock;
  
  int sid;

  SIBMaxFd *maxfd;
};

typedef struct _ThreadArgs
{
  NotaRMHandler *handler;
  gchar *uuid;
} ThreadArgs;

#if USE_RM == 1
static gint create_rm_socket( NotaRMHandler *self );
static gpointer nota_rm_handler_register_sib_thread(gpointer data);
static int nota_rm_handler_deregister_service(NotaRMHandler *self);
#endif

NotaRMHandler *nota_rm_handler_new(SIBMaxFd *maxfd)
{
  
  NotaRMHandler *self = NULL;
  whiteboard_log_debug_fb();
  self = g_new0(NotaRMHandler, 1);
  self->fd = -1;
  self->maxfd = maxfd;
  whiteboard_log_debug_fe();
  return self;
}

void nota_rm_handler_destroy(NotaRMHandler *self)
{
  h_in_t *core = Hgetinstance();
  whiteboard_log_debug_fb();

  if(self->sid)
    {
      nota_rm_handler_deregister_service(self);
    }

  if(self->dereg_cert)
    {
      g_free(self->dereg_cert);
    }
  if(self->dereg_act)
    {
      g_free(self->dereg_act);
    }

  if(self->cp)
    {
      nota_stub_context_pointer_free(self->cp);
    }
  
  if(self->fd > 0 )
    {
      Hclose(core, self->fd);
      self->fd = -1;
    }
  
  g_free(self);
  
  whiteboard_log_debug_fe();
}

gint nota_rm_handler_register_sib(NotaRMHandler *self, gchar *uuid)
{
  ThreadArgs *ta = NULL;
  int ret = -1;
  whiteboard_log_debug_fb();
#if USE_RM==1
  ta = g_new0(ThreadArgs,1);
  ta->handler = self;
  ta->uuid = g_strdup(uuid);
  
  ret = GPOINTER_TO_INT(nota_rm_handler_register_sib_thread(ta));
  if(ret == 0)
    {
      ret = self->sid;
      whiteboard_log_debug("Got SID %d\n", ret);
    }
#else
  ret = TEST_SID;
#endif  
  
  whiteboard_log_debug_fe();
  return ret;
				       
}
#if USE_RM == 1
gpointer nota_rm_handler_register_sib_thread( gpointer data)
{
  gint err;
  int max_fd;
  fd_set open_sockets;
  fd_set error_set;
  h_in_t* core = NULL;
  ThreadArgs* ta = (ThreadArgs *)data;
  NotaRMHandler *self = ta->handler;
  struct timeval tv;

  gchar *oname;
  gchar *cert;

  
  whiteboard_log_debug_fb();
  
  if( self->fd < 0)
    {
      whiteboard_log_debug("RM socket not created, trying to open..\n");
      if( create_rm_socket(self) < 0)
	{
	  whiteboard_log_debug("RM socket open FAIL\n");	        
	  return GINT_TO_POINTER(-1);
	}
      else
	{
	  whiteboard_log_debug("RM socket open OK, got socket: %d\n",self->fd);
	}
    }
  else
    {
      whiteboard_log_debug("Using existing RM socket %d\n", self->fd);
    }
  oname = g_strndup(M3_ONTOLOGY_NAME, strlen(M3_ONTOLOGY_NAME));
  cert = g_strndup( CERT_DUMMY, strlen(CERT_DUMMY));
  err =
    NoTA_System_ResourceMng_R01_01_Register_req(self->cp,
						(uint8_t *) oname,
						(uint16_t) strlen(M3_ONTOLOGY_NAME) ,
						(uint8_t *) ta->uuid,
						(uint16_t) strlen(ta->uuid),
						(uint8_t *) cert,
						(uint16_t) strlen(CERT_DUMMY));

  g_free(oname);
  g_free(cert);
  if(err < 0)
    {
      whiteboard_log_warning("Could not send RegisterReq message\n");
      return GINT_TO_POINTER(err);
    }
  
  whiteboard_log_debug("Register Req sent, waiting for response\n");
  
  FD_ZERO(&open_sockets);
  FD_ZERO(&error_set);
  FD_SET(self->fd, &open_sockets);
  FD_SET(self->fd, &error_set);
  core = Hgetinstance();
  
  tv.tv_sec = 1; // 1 second timeout
  tv.tv_usec = 0;
  max_fd  = sib_maxfd_lock_and_get( self->maxfd);
  err = Hselect(core, max_fd+1, &open_sockets, NULL, &error_set, &tv);
  sib_maxfd_unlock(self->maxfd);
  if(err < 0)
    {
      whiteboard_log_warning("RM select: %s\n", strerror(errno));
    }
  else
    {
      if(FD_ISSET(self->fd, &error_set))
	{
	  whiteboard_log_warning("Error on socket: %d\n", errno);
	}
      else if(FD_ISSET(self->fd, &open_sockets))
	{
	  err = nota_stub_handle_incoming(self->cp);
	  if(err < 0)
	    {
	      whiteboard_log_warning("nota_stub_handle_incoming :%d", errno);
	    }
	}
      else
	{
	  whiteboard_log_debug("Select timeout, Register conf not received\n");
	  err = -1;
	}
    }

  g_free(ta->uuid);
  g_free(ta);
  whiteboard_log_debug_fe();
  return GINT_TO_POINTER(err);
}

gint nota_rm_handler_deregister_service(NotaRMHandler *self)
{
  int max_fd;
  fd_set open_sockets;
  fd_set error_set;
  h_in_t* core = NULL;
  struct timeval tv;
  gint err;
  if(self->sid<=0)
    {
      whiteboard_log_warning("nota_rm_handler_deregister_service: not registered\n");	        
      return -1;
    }

  if( self->fd < 0)
    {
      if( create_rm_socket(self) < 0)
	{
	  whiteboard_log_warning("nota_rm_handler_deregister_service_thread: RM socket open FAIL\n");	        
	  return -1;
	}
      else
	{
	 whiteboard_log_debug("RM socket open OK, got socket: %d\n",self->fd);
	}
    }
  else
    {
      whiteboard_log_debug("Using existing RM socket %d\n", self->fd);
    }
  err =
    NoTA_System_ResourceMng_R01_01_Deregister_req(self->cp,
						  self->sid,
						  (uint8_t *)self->dereg_cert,
						  (uint16_t )strlen(self->dereg_cert));
  
  
  if(err < 0)
    {
      whiteboard_log_debug("nota_rm_handler_deregister_service: Could not send DeregisterReq message\n");
      // close RM connection after registration
      NoTA_System_ResourceMng_R01_01_user_remove_connection(self->cp);
      nota_stub_context_pointer_free(self->cp);
      self->cp = NULL;
      Hclose(core, self->fd);
      self->fd = -1;
      return err;
    }
  
  whiteboard_log_debug("Deregister Req sent, waiting for response\n");
  

  err = nota_stub_handle_incoming(self->cp);

#if 0  
  core = Hgetinstance();
  FD_ZERO(&open_sockets);
  FD_ZERO(&error_set);
  FD_SET(self->fd, &open_sockets);
  FD_SET(self->fd, &error_set);

  max_fd  = sib_maxfd_lock_and_get( self->maxfd);
  err = Hselect(core, max_fd+1, &open_sockets, NULL, &error_set, NULL);
  sib_maxfd_unlock(self->maxfd);
  if(err < 0)
    {
      whiteboard_log_debug("RM select: %s\n", strerror(errno));
    }
  else
    {
      if(FD_ISSET(self->fd, &error_set))
	{
	  whiteboard_log_debug("Error on socket: %d\n", errno);
	}
      else if(FD_ISSET(self->fd, &open_sockets))
	{
	  err = nota_stub_handle_incoming(self->cp);
	  if(err < 0)
	    {
	      whiteboard_log_debug("nota_stub_handle_incoming :%d", errno);
	    }
	}
      else
	{
	  whiteboard_log_debug("Select timeout, Register conf not received\n");
	  err = -1;
	}
    }
#endif 
  self->sid = -1;
}

#endif

#if 0
static gint deregister_handler( ThreadArgs *ta )
{
  gint err;
  fd_set open_sockets;
  fd_set error_set;
  h_in_t* core = NULL;;

  
  
  err =
    NoTA_System_ResourceMng_R01_01_Deregister_req(ta->cp,
						  ta->sid,
						  (uint8_t*)dereg_cert,
						  (uint16_t)strlen(dereg_cert));
  
  if(err < 0)
    return err;

  
  FD_ZERO(&open_sockets);
  FD_ZERO(&error_set);
  FD_SET(ta->socket, &open_sockets);
  FD_SET(ta->socket, &error_set);
  core = Hgetinstance();
  //  whiteboard_log_debug("register_handler: entering select loop, fd: %d, core: %p\n", ta->socket, core);
  while(deregConfReceived==0)
    {
      err = Hselect(core, ta->socket+1, &open_sockets, NULL, &error_set, NULL);
      if(err < 0)
	{
	  perror("select: ");
	  break;
	}
      
      if(FD_ISSET(ta->socket, &error_set))
	{
	  whiteboard_log_debug("Error on socket\n");
	  break;
	}
      else if(FD_ISSET(ta->socket, &open_sockets))
	{
	  err = nota_stub_handle_incoming(ta->cp);
	  if(err < 0)
	    {
	      perror("Receive:");
	      
	      whiteboard_log_debug("Closing client(%d)\n",err);
	      break;
	    }
	}
      else
	{
	  whiteboard_log_debug("Error on select\n");
	  break;
	}
    }
  
  return err;
}
#endif	


#if USE_RM == 1

gint create_rm_socket(NotaRMHandler *self)
{
  nota_addr_t addr = { SID_RM, 0};
  h_in_t *core = Hgetinstance();
  whiteboard_log_debug_fb();
  if(!core)
    {
      whiteboard_log_debug("Could not get H_IN core pointer.\n");
      return -1;
    }
  
  
  self->fd = Hsocket(core, AF_NOTA, SOCK_STREAM, 0);
  if(self->fd <=0)
    {
      whiteboard_log_debug("Could not get H_IN socket.\n");
      self->fd = -1;
      return -1;
    }
  
  if( Hconnect(core, self->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
      whiteboard_log_debug("Could not connect\n");
      Hclose(core, self->fd);
      self->fd = -1;
      return -1;
    }
  whiteboard_log_debug("Socket connected(%d)\n", self->fd);
  self->cp = nota_stub_context_pointer_initialize(core, self->fd);
  if(!self->cp)
    {
      whiteboard_log_debug("Could not create STUB context pointer\n");
      Hclose(core, self->fd);
      self->fd = -1;
      return -1;
    }
  
  whiteboard_log_debug("STUB context inialized\n");
  if( NoTA_System_ResourceMng_R01_01_user_new_connection(self->cp) < 0)
    {
      whiteboard_log_debug("could not add to stub\n");
      nota_stub_context_pointer_free(self->cp);
      self->cp = NULL;
      Hclose(core, self->fd);
      self->fd = -1;
      return -1;
    }
  self->cp->user_pointer = self;
  sib_maxfd_set(self->maxfd, self->fd);
  whiteboard_log_debug(" Stub connected for socket: %d\n", self->fd);
  whiteboard_log_debug_fe();
  return 0;
}




void NoTA_System_ResourceMng_R01_01_Register_cnf_process( struct context_pointer* context, status_t status, sid_t sid, uint8_t* cert_dereg, uint16_t cert_dereg_len, uint8_t* cert_act, uint16_t cert_act_len )
{
  NotaRMHandler *self = (NotaRMHandler *)context->user_pointer;
  whiteboard_log_debug_fb();
  self->regConfReceived = 1;
  if(status==S_OK)
    {
      self->dereg_cert = g_strndup( (gchar *)cert_dereg, cert_dereg_len);
      self->dereg_act = g_strndup( (gchar *)cert_act, cert_act_len);
      self->sid = sid;
    }
  whiteboard_log_debug_fe();

}

void NoTA_System_ResourceMng_R01_01_Deregister_cnf_process( struct context_pointer* context, status_t status )
{
  NotaRMHandler *self = (NotaRMHandler *)context->user_pointer;
  self->deregConfReceived = 1;
  whiteboard_log_debug("NoTA_System_ResourceMng_R01_01_Deregister_cnf_process: status: %d\n", status);
}

void NoTA_System_ResourceMng_R01_01_Challenge_ind_process( struct context_pointer* context, uint8_t* challenge, uint16_t challenge_len )
{
  whiteboard_log_debug("NoTA_System_ResourceMng_R01_01_Challenge_ind_process\n");
}

void NoTA_System_ResourceMng_R01_01_ResolveService_cnf_process( struct context_pointer* context, status_t status, sid_t sid )
{
whiteboard_log_debug("NoTA_System_ResourceMng_R01_01_ResolveService_cnf_process\n");
}

void NoTA_System_ResourceMng_R01_01_ListOfServices_cnf_process( struct context_pointer* context, servicelist_t* servicelist )
{
whiteboard_log_debug("NoTA_System_ResourceMng_R01_01_ListOfServices_cnf_process\n");
}

void NoTA_System_ResourceMng_R01_01_NewEvent_cnf_process( struct context_pointer* context, int32_t event_id, status_t status )
{
  whiteboard_log_debug("NoTA_System_ResourceMng_R01_01_NewEvent_cnf_process\n");
}

void NoTA_System_ResourceMng_R01_01_NewEvent_ind_process( struct context_pointer* context, uint32_t event_id, event_t event_t )
{
whiteboard_log_debug("NoTA_System_ResourceMng_R01_01_NewEvent_ind_process\n");
}

void NoTA_System_ResourceMng_R01_01_DeleteEvent_cnf_process( struct context_pointer* context, status_t status )
{
whiteboard_log_debug("NoTA_System_ResourceMng_R01_01_DeleteEvent_cnf_process\n");
}


/* these hooks must be implemented by user. */
/* This function is called when connection was closed */
void NoTA_System_ResourceMng_R01_01_user_handler_disconnected(struct context_pointer* context)
{
       whiteboard_log_debug("NoTA_System_ResourceMng_R01_01_user_handler_disconnect\n");
}


/* This function is called when there was out-of-memory or other not so critical error (connection is still usable) */
void NoTA_System_ResourceMng_R01_01_user_handler_error(struct context_pointer* context, int error_type)
{
  
  whiteboard_log_debug("NoTA_System_ResourceMng_R01_01_user_handler_error\n");

}
#endif

SIBMaxFd *nota_rm_handler_get_maxfd(NotaRMHandler *self)
{
  return self->maxfd;
}
