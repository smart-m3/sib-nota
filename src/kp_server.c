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

#include <sib_object.h>
#include <whiteboard_log.h>
#include <uuid/uuid.h>
#include <h_in/h_bsdapi.h>

#include "kp_server.h"
#include "SibAccessNota_service.h"

struct _KPServer
{
  SibObject *sib;
  int fd;
  struct context_pointer *cp;
  GMutex *send_lock;
};

static void kp_server_handle_join_cnf( SibObject *context,
				       guchar *spaceid,
				       guchar *nodeid,
				       gint msgnum,
				       gint success,
				       guchar *credentials,
				       gpointer user_data);

static void kp_server_handle_leave_cnf( SibObject *context,
					guchar *spaceid,
					guchar *nodeid,
					gint msgnum,
					gint success,
					gpointer user_data);

static void kp_server_handle_insert_cnf( SibObject *context,
					 guchar *spaceid,
					 guchar *nodeid,
					 gint msgnum,
					 gint success,
					 guchar *bNodes,
					 gpointer user_data);
static void kp_server_handle_remove_cnf( SibObject *context,
					 guchar *spaceid,
					 guchar *nodeid,
					 gint msgnum,
					 gint success,
					 gpointer user_data);
static void kp_server_handle_update_cnf( SibObject *context,
					 guchar *spaceid,
					 guchar *nodeid,
					 gint msgnum,
					 gint success,
					 guchar *bNodes,
					 gpointer user_data);
static void kp_server_handle_query_cnf( SibObject *context,
					guchar *spaceid,
					guchar *nodeid,
					gint msgnum,
					gint success,
					guchar *results,
					gpointer user_data);
static void kp_server_handle_subscribe_cnf( SibObject *context,
					    guchar *spaceid,
					    guchar *nodeid,
					    gint msgnum,
					    gint success,
					    guchar *subscription_id,
					    guchar *results,
					    gpointer user_data);
static void kp_server_handle_subscription_ind( SibObject *context,
					       guchar *spaceid,
					       guchar *nodeid,
					       gint msgnum,
					       gint seqnum,
					       guchar *subscription_id,
					       guchar *results_new,
					       guchar *results_obsolete,
					       gpointer user_data);

static void kp_server_handle_unsubscribe_cnf( SibObject *context,
					      guchar *spaceid,
					      guchar *nodeid,
					      gint msgnum,
					      gint status,
					      guchar *subscription_id,
					      gpointer user_data);
static void kp_server_handle_unsubscribe_ind( SibObject *context,
					      guchar *spaceid,
					      guchar *nodeid,
					      gint msgnum,
					      gint status,
					      guchar *subscription_id,
					      gpointer user_data);
static void kp_server_handle_leave_ind( SibObject *context,
					guchar *spaceid,
					guchar *nodeid,
					gint msgnum,
					gint status,
					gpointer user_data);


KPServer *kp_server_new(int fd, KPListener *listener)
{
  KPServer *self = NULL;
  h_in_t *core = Hgetinstance();
  SibObject *object = NULL;
  gchar tmp[37];
  uuid_t u1;
  int err = -1;
  whiteboard_log_debug_fb();

  uuid_generate(u1);

  uuid_unparse(u1, tmp);

  object = SIB_OBJECT( sib_object_new( tmp, /*uuid*/
				       NULL, /* control channel uuid */ 
				       NULL, /* main_context */
				       "N/A", /* Friendly name */
				       "Not implemented")); /* desription */
  
  g_return_val_if_fail(object != NULL,NULL);


  
  self = g_new0(KPServer,1);
  self->sib = object;
  self->fd = fd;
  self->cp = nota_stub_context_pointer_initialize(core, fd);

  if(self->cp)
    {
      err =  SibAccessNota_service_new_connection(self->cp);
      if(err < 0)
	{
	  whiteboard_log_debug("Could not initialize connection");
	  nota_stub_context_pointer_free( self->cp);
	}
      else
	{
	  whiteboard_log_debug("Connection created for fd %d\n",fd);
	  self->cp->user_pointer = listener;
	}
    }
  else
    {
      whiteboard_log_debug("Could not initialize context");
    }
  
  if(err<0)
    {
      Hclose(core, fd);
      g_object_unref( G_OBJECT(self->sib));
      g_free(self);
      self=NULL;
    }
  else
    {
      self->send_lock = g_mutex_new();
      // connect callback handlers
      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_JOIN_CNF,
			(GCallback)kp_server_handle_join_cnf,
			self);

      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_LEAVE_CNF,
			(GCallback)kp_server_handle_leave_cnf,
			self);
      
      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_INSERT_CNF,
			(GCallback)kp_server_handle_insert_cnf,
			self);

      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_REMOVE_CNF,
			(GCallback)kp_server_handle_remove_cnf,
			self);
      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_UPDATE_CNF,
			(GCallback)kp_server_handle_update_cnf,
			self);
      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_QUERY_CNF,
			(GCallback)kp_server_handle_query_cnf,
			self);
      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_SUBSCRIBE_CNF,
			(GCallback)kp_server_handle_subscribe_cnf,
			self);
      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_SUBSCRIPTION_IND,
			(GCallback)kp_server_handle_subscription_ind,
			self);

      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_UNSUBSCRIBE_IND,
			(GCallback)kp_server_handle_unsubscribe_ind,
			self);
      
      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_UNSUBSCRIBE_CNF,
			(GCallback)kp_server_handle_unsubscribe_cnf,
			self);
      
      g_signal_connect( G_OBJECT(object),
			SIB_OBJECT_SIGNAL_LEAVE_IND,
			(GCallback)kp_server_handle_leave_ind,
			self);
      
      whiteboard_log_debug("Connection ready\n");
    }
  whiteboard_log_debug_fe();
  return ( (err<0 )?NULL:self);
}

void kp_server_destroy(KPServer *self)
{
  h_in_t *core = Hgetinstance();
  whiteboard_log_debug_fb();

  g_mutex_lock( self->send_lock);
  
  nota_stub_context_pointer_free(self->cp);
  Hclose(core, self->fd);
  g_object_unref( G_OBJECT(self->sib));
  g_mutex_unlock( self->send_lock);
  g_mutex_free(self->send_lock);
  g_free(self);
  whiteboard_log_debug_fe();

}

gint kp_server_handle_incoming( KPServer *self)
{
  gint retval;
  whiteboard_log_debug_fb();
  retval = nota_stub_handle_incoming( self->cp );
  whiteboard_log_debug_fe();
  return retval;
}

gint kp_server_send_join( KPServer *self, guchar *spaceid, guchar *nodeid, gint msgnum, guchar *credentials)
{
  gint retval;
  whiteboard_log_debug_fb();
  retval = sib_object_send_join( self->sib, spaceid, nodeid, msgnum, credentials);
  whiteboard_log_debug_fe();
  return retval;
}

gint kp_server_send_leave( KPServer *self, guchar *spaceid, guchar *nodeid, gint msgnum)
{
  gint retval;
  whiteboard_log_debug_fb();
  retval = sib_object_send_leave( self->sib, spaceid, nodeid, msgnum);
  whiteboard_log_debug_fb();
  return retval;
}

gint kp_server_send_insert( KPServer *self, guchar *spaceid, guchar *nodeid, gint msgnum, EncodingType encoding, guchar *insert_graph)
{
  gint retval;
  whiteboard_log_debug_fb();
  retval = sib_object_send_insert( self->sib, spaceid, nodeid, msgnum, encoding, insert_graph);
  whiteboard_log_debug_fe();
  return retval;
}


gint kp_server_send_remove( KPServer *self, guchar *spaceid, guchar *nodeid, gint msgnum, EncodingType encoding, guchar *remove_graph)
{
  gint retval;
  whiteboard_log_debug_fb();
  retval = sib_object_send_remove( self->sib, spaceid, nodeid, msgnum, encoding, remove_graph);
  whiteboard_log_debug_fe();
  return retval;
}

gint kp_server_send_update( KPServer *self, guchar *spaceid, guchar *nodeid, gint msgnum, EncodingType encoding, guchar *insert_graph, guchar *remove_graph)
{
  gint retval;
  whiteboard_log_debug_fb();
  retval = sib_object_send_update( self->sib, spaceid, nodeid, msgnum, encoding, insert_graph, remove_graph);
  whiteboard_log_debug_fe();
  return retval;
}

gint kp_server_send_query( KPServer *self, guchar *spaceid, guchar *nodeid, gint msgnum, QueryType type, guchar *query)
{
  gint retval;
  whiteboard_log_debug_fb();
  retval = sib_object_send_query( self->sib, spaceid, nodeid, msgnum, type, query);
  whiteboard_log_debug_fe();
  return retval;
}

gint kp_server_send_subscribe( KPServer *self, guchar *spaceid, guchar *nodeid, gint msgnum, QueryType type, guchar *query)
{
  gint retval;
  whiteboard_log_debug_fb();
  retval = sib_object_send_subscribe( self->sib, spaceid, nodeid, msgnum, type, query);
  whiteboard_log_debug_fe();
  return retval;
}
gint kp_server_send_unsubscribe( KPServer *self, guchar *spaceid, guchar *nodeid, gint msgnum, guchar *subscription_id)
{
  gint retval;
  whiteboard_log_debug_fb();
  retval = sib_object_send_unsubscribe( self->sib, spaceid, nodeid, msgnum, subscription_id);
  whiteboard_log_debug_fe();
  return retval;
}

void kp_server_handle_join_cnf( SibObject *context,
				guchar *spaceid,
				guchar *nodeid,
				gint msgnum,
				gint success,
				guchar *credentials,
				gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;
  
  g_mutex_lock(self->send_lock);

  whiteboard_log_debug("JoinCnf ->\n");
  whiteboard_log_debug("\tspaceid: %s\n", spaceid);
  whiteboard_log_debug("\tnodeid: %s\n", nodeid);
  whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
  whiteboard_log_debug("\tsuccess: %d\n", success);

  if( SibAccessNota_Join_cnf(self->cp,
			     (uint8_t *) nodeid,
			     (uint16_t) strlen( (gchar *)nodeid),
			     (uint8_t *) spaceid,
			     (uint16_t) strlen( (gchar *)spaceid),
			     (uint16_t) msgnum,
			     (sibstatus_t) success,
			     (uint8_t *) credentials,
			     (uint16_t) strlen( (gchar *)credentials)) < 0 )
    {
      whiteboard_log_warning("Join cnf sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Join cnf sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}


void kp_server_handle_leave_cnf( SibObject *context,
				 guchar *spaceid,
				 guchar *nodeid,
				 gint msgnum,
				 gint success,
				 gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;
  
  g_mutex_lock(self->send_lock);
    whiteboard_log_debug("LeaveCnf ->\n");
  whiteboard_log_debug("\tspaceid: %s\n", spaceid);
  whiteboard_log_debug("\tnodeid: %s\n", nodeid);
  whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
  whiteboard_log_debug("\tsuccess: %d\n", success);
  if( SibAccessNota_Leave_cnf(self->cp,
			      (uint8_t *) nodeid,
			      (uint16_t) strlen((gchar *)nodeid),
			      (uint8_t *) spaceid,
			      (uint16_t) strlen((gchar *)spaceid),
			      (uint16_t) msgnum,
			      (sibstatus_t) success ) < 0 )
    {
      whiteboard_log_warning("Leave cnf sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Leave cnf sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}

void kp_server_handle_remove_cnf( SibObject *context,
				  guchar *spaceid,
				  guchar *nodeid,
				  gint msgnum,
				  gint success,
				  gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;
  
  g_mutex_lock(self->send_lock);
  whiteboard_log_debug("RemoveCnf ->\n");
  whiteboard_log_debug("\tspaceid: %s\n", spaceid);
  whiteboard_log_debug("\tnodeid: %s\n", nodeid);
  whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
  whiteboard_log_debug("\tsuccess: %d\n", success);  
  if( SibAccessNota_Remove_cnf(self->cp,
			       (uint8_t *) nodeid,
			       (uint16_t) strlen( (gchar *)nodeid),
			       (uint8_t *) spaceid,
			       (uint16_t) strlen( (gchar *)spaceid),
			       (uint16_t) msgnum,
			       (sibstatus_t) success) < 0 )
    {
      whiteboard_log_warning("Remove cnf sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Remove cnf sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}

void kp_server_handle_update_cnf( SibObject *context,
				  guchar *spaceid,
				  guchar *nodeid,
				  gint msgnum,
				  gint success,
				  guchar *bNodes,
				  gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;

  g_mutex_lock(self->send_lock);
  whiteboard_log_debug("UpdateCnf ->\n");
  whiteboard_log_debug("\tspaceid: %s\n", spaceid);
  whiteboard_log_debug("\tnodeid: %s\n", nodeid);
  whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
  whiteboard_log_debug("\tsuccess: %d\n", success);
  whiteboard_log_debug("\tbNodes: %s\n", bNodes);
  if( SibAccessNota_Update_cnf(self->cp,
			       (uint8_t *) nodeid,
			       (uint16_t) strlen( (gchar *)nodeid),
			       (uint8_t *) spaceid,
			       (uint16_t) strlen( (gchar *)spaceid),
			       (uint16_t) msgnum,
			       (sibstatus_t) success,
			       (uint8_t *) bNodes,
			       (uint16_t) strlen( (gchar *)bNodes)) < 0 )
    {
      whiteboard_log_warning("Update cnf sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Update cnf sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}

void kp_server_handle_insert_cnf( SibObject *context,
				  guchar *spaceid,
				  guchar *nodeid,
				  gint msgnum,
				  gint success,
				  guchar *bNodes,
				  gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;
  
  g_mutex_lock(self->send_lock);
   whiteboard_log_debug("InsertCnf ->\n");
  whiteboard_log_debug("\tspaceid: %s\n", spaceid);
  whiteboard_log_debug("\tnodeid: %s\n", nodeid);
  whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
  whiteboard_log_debug("\tsuccess: %d\n", success);
  whiteboard_log_debug("\tbNodes: %s\n", bNodes);
  if( SibAccessNota_Insert_cnf(self->cp,
			       (uint8_t *) nodeid,
			       (uint16_t) strlen( (gchar *)nodeid),
			       (uint8_t *) spaceid,
			       (uint16_t) strlen( (gchar *)spaceid),
			       (uint16_t) msgnum,
			       (sibstatus_t) success,
			       (uint8_t *) bNodes,
			       (uint16_t) strlen( (gchar *)bNodes)) < 0 )
    {
      whiteboard_log_warning("Insert cnf sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Insert cnf sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}

void kp_server_handle_query_cnf( SibObject *context,
				 guchar *spaceid,
				 guchar *nodeid,
				 gint msgnum,
				 gint success,
				 guchar *results,
				 gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;
  
  g_mutex_lock(self->send_lock);
   whiteboard_log_debug("QueryCnf ->\n");
  whiteboard_log_debug("\tspaceid: %s\n", spaceid);
  whiteboard_log_debug("\tnodeid: %s\n", nodeid);
  whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
  whiteboard_log_debug("\tsuccess: %d\n", success);
  whiteboard_log_debug("\tresults: %s\n", results);
  if( SibAccessNota_Query_cnf(self->cp,
			      (uint8_t *) nodeid,
			      (uint16_t) strlen( (gchar *)nodeid),
			      (uint8_t *) spaceid,
			      (uint16_t) strlen( (gchar *)spaceid),
			      (uint16_t) msgnum,
			      (sibstatus_t) success,
			      (uint8_t *) results,
			      (uint16_t) strlen( (gchar *)results)) < 0 )
    {
      whiteboard_log_warning("Query cnf sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Query cnf sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}

void kp_server_handle_subscribe_cnf( SibObject *context,
				     guchar *spaceid,
				     guchar *nodeid,
				     gint msgnum,
				     gint success,
				     guchar *subscription_id,
				     guchar *results,
				     gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;
  
  g_mutex_lock(self->send_lock);
   whiteboard_log_debug("SubscribeCnf ->\n");
  whiteboard_log_debug("\tspaceid: %s\n", spaceid);
  whiteboard_log_debug("\tnodeid: %s\n", nodeid);
  whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
  whiteboard_log_debug("\tsuccess: %d\n", success);
  whiteboard_log_debug("\tsubid: %s\n", subscription_id);
  whiteboard_log_debug("\tbresults: %s\n", results);
  if( SibAccessNota_Subscribe_cnf(self->cp,
				  (uint8_t *) nodeid,
				  (uint16_t) strlen( (gchar *)nodeid),
				  (uint8_t *) spaceid,
				  (uint16_t) strlen( (gchar *)spaceid),
				  (uint16_t) msgnum,
				  (sibstatus_t) success,
				  (uint8_t *) subscription_id,
				  (uint16_t) strlen( (gchar *)subscription_id),
				  (uint8_t *) results,
				  (uint16_t) strlen( (gchar *)results)) < 0 )
    {
      whiteboard_log_warning("Subsribe cnf sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Subscribe cnf sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}

void kp_server_handle_subscription_ind( SibObject *context,
					guchar *spaceid,
					guchar *nodeid,
					gint msgnum,
					gint seqnum,
					guchar *subscription_id,
					guchar *results_new,
					guchar *results_obsolete,
					gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;
  
  g_mutex_lock(self->send_lock);
  whiteboard_log_debug("SubscribeInd ->\n");
  whiteboard_log_debug("\tspaceid: %s\n", spaceid);
  whiteboard_log_debug("\tnodeid: %s\n", nodeid);
  whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
  whiteboard_log_debug("\tseqnum: %d\n", seqnum);
  whiteboard_log_debug("\tsubid: %s\n", subscription_id);
  whiteboard_log_debug("\tbresults_new: %s\n", results_new);
  whiteboard_log_debug("\tbresults_obs: %s\n", results_obsolete);

  if( SibAccessNota_Subscription_ind(self->cp,
				     (uint8_t *) nodeid,
				     (uint16_t) strlen( (gchar *)nodeid),
				     (uint8_t *) spaceid,
				     (uint16_t) strlen( (gchar *)spaceid),
				     (uint16_t) msgnum,
				     (uint16_t) seqnum,
				     (uint8_t *) subscription_id,
				     (uint16_t) strlen( (gchar *)subscription_id),
				     (uint8_t *) results_new,
				     (uint16_t) strlen( (gchar *)results_new),
				     (uint8_t *) results_obsolete,
				     (uint16_t) strlen( (gchar *)results_obsolete)) < 0 )
    {
      whiteboard_log_warning("Subsription ind sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Subscription ind sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}

void kp_server_handle_unsubscribe_cnf( SibObject *context,
				       guchar *spaceid,
				       guchar *nodeid,
				       gint msgnum,
				       gint status,
				       guchar *subscription_id,
				       gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;
  
  g_mutex_lock(self->send_lock);
   whiteboard_log_debug("UnsubscribeCnf ->\n");
  whiteboard_log_debug("\tspaceid: %s\n", spaceid);
  whiteboard_log_debug("\tnodeid: %s\n", nodeid);
  whiteboard_log_debug("\tmsgnum: %d\n", msgnum);
  whiteboard_log_debug("\tsutccess: %d\n", status);
  whiteboard_log_debug("\tsubid: %s\n", subscription_id);

  if( SibAccessNota_Unsubscribe_cnf(self->cp,
				    (uint8_t *) nodeid,
				    (uint16_t) strlen( (gchar *)nodeid),
				    (uint8_t *) spaceid,
				    (uint16_t) strlen( (gchar *)spaceid),
				    (uint16_t) msgnum,
				    status) < 0)
    {
      whiteboard_log_warning("Unsubsrcibe cnf sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Unsubscribe cnf sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}

void kp_server_handle_unsubscribe_ind( SibObject *context,
				       guchar *spaceid,
				       guchar *nodeid,
				       gint msgnum,
				       gint status,
				       guchar *subscription_id,
				       gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;
  
  g_mutex_lock(self->send_lock);
  
  if( SibAccessNota_Unsubscribe_ind(self->cp,
				    (uint8_t *) nodeid,
				    (uint16_t) strlen( (gchar *)nodeid),
				     (uint8_t *) spaceid,
				     (uint16_t) strlen( (gchar *)spaceid),
				    (uint16_t) msgnum,
				    status,
				    (uint8_t *) subscription_id,
				    (uint16_t) strlen( (gchar *)subscription_id) ) < 0)
    {
      whiteboard_log_warning("Unsubscribe ind sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Unsubscribe ind sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}


void kp_server_handle_leave_ind( SibObject *context,
				 guchar *spaceid,
				 guchar *nodeid,
				 gint msgnum,
				 gint status,
				 gpointer user_data)
{
  KPServer *self = NULL;
  whiteboard_log_debug_fb();
  self = (KPServer *)user_data;
  
  g_mutex_lock(self->send_lock);
  
  if( SibAccessNota_Leave_ind(self->cp,
			      (uint8_t *) nodeid,
			      (uint16_t) strlen( (gchar *)nodeid),
			      (uint8_t *) spaceid,
			      (uint16_t) strlen( (gchar *)spaceid),
			      (uint16_t) msgnum,
			      status)  <  0 )
    {
      whiteboard_log_warning("Leave ind sending failed\n");
    }
  else
    {
      whiteboard_log_debug("Leave ind sent\n");
    }
  g_mutex_unlock(self->send_lock);
  whiteboard_log_debug_fe();
}

