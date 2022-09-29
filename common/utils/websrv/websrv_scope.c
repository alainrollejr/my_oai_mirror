/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file common/utils/websrv/websrv_scope.c
 * \brief: implementation of web API specific for oai softscope
 * \author Francois TABURET
 * \date 2022
 * \version 0.1
 * \company NOKIA BellLabs France
 * \email: francois.taburet@nokia-bell-labs.com
 * \note
 * \warning
 */
 #include <libgen.h>
 #include <jansson.h>
 #include <ulfius.h>
 #include <gnutls/gnutls.h>
 #include <gnutls/x509.h>
 #include "common/config/config_userapi.h"
 #include "common/utils/LOG/log.h"
 #include "common/utils/websrv/websrv.h"
 #include "executables/softmodem-common.h"
 #include "common/ran_context.h"
 #include "common/utils/websrv/websrv_noforms.h"
 #include "openair1/PHY/TOOLS/phy_scope.h"
 #include "openair1/PHY/TOOLS/phy_scope_interface.h" 
 #include "common/utils/load_module_shlib.h"
 
 extern PHY_VARS_NR_UE ***PHY_vars_UE_g;
 
static scopeData_t  scopedata; 
static websrv_scope_params_t scope_params = {0,1000,NULL,&scopedata,65535};
static websrv_params_t *websrvparams_ptr;
static int cansend=1;
void  websrv_scope_senddata(int numd, int dsize, websrv_scopedata_msg_t *msg) {
/* 

  for ( int i=0; i<n; i++) {  
    msg->data_xy[2*i]=(i>(n/2))? 10 : -10;
    msg->data_xy[(2*i)+1]= (i>(n/4))? 10 : -10; 
  }*/
  msg->header.src=WEBSOCK_SRC_SCOPE ;
  if (cansend || !(scope_params.statusmask & SCOPE_STATUSMASK_DATAACK)) {
    int st = ulfius_websocket_send_message( websrvparams_ptr->wm, U_WEBSOCKET_OPCODE_BINARY,(numd*dsize)+WEBSOCK_HEADSIZE, (char *)msg);
    if (st != U_OK)
      LOG_I(UTIL, "Error sending scope IQs, status %i\n",st);
    cansend=0; 
  }  
};


void websrv_websocket_send_scopemessage(char msg_type, char *msg_data, struct _websocket_manager * websocket_manager) {
  websrv_websocket_send_message(WEBSOCK_SRC_SCOPE,msg_type, msg_data,websrvparams_ptr->wm)  ;
}

void websrv_websocket_process_scopemessage(char msg_type, char *msg_data, struct _websocket_manager * websocket_manager) {
  LOG_I(UTIL,"[websrv] processing scope message type %i\n", msg_type);
  switch ( msg_type ) {
 
    case SCOPEMSG_TYPE_STATUSUPD:
      if (strncmp(msg_data,"disabled",8) == 0){
		LOG_I(UTIL,"[websrv] SoftScope disabled state client ack  \n");  
        scope_params.statusmask = SCOPE_STATUSMASK_DISABLED;
      }	    
      break;
    case SCOPEMSG_TYPE_DATAACK:
      cansend=1;
      break;      
    default:
      LOG_W(UTIL,"[websrv] Unprocessed scope message type: %c /n",msg_type);
      break;
  }
}

int websrv_scope_manager(uint64_t lcount,websrv_params_t *websrvparams) {
  time_t linuxtime;
  struct tm loctime;
  char strtime[64];
    if (scope_params.statusmask & SCOPE_STATUSMASK_STARTED) {
	  if( lcount%10 == 0 ) {	
		linuxtime=time(NULL); 
        localtime_r(&linuxtime,&loctime);
        snprintf(strtime,sizeof(strtime),"%d/%d/%d %d:%d:%d",loctime.tm_mday,loctime.tm_mon,loctime.tm_year+1900,loctime.tm_hour,loctime.tm_min,loctime.tm_sec);		
	    websrv_websocket_send_scopemessage(SCOPEMSG_TYPE_TIME, strtime, websrvparams_ptr->wm);
	  }
      if ( (lcount % scope_params.refrate) == 0) {        
		  if (IS_SOFTMODEM_GNB_BIT) {			 
            phy_scope_gNB(scope_params.scopeform,  scope_params.scopedata, 1);
		  } 
		  if (IS_SOFTMODEM_5GUE_BIT) {
            phy_scope_nrUE(scope_params.scopeform, (PHY_VARS_NR_UE *)scope_params.scopedata,  0, scope_params.selectedTarget);
		  }      
      } 
    }
    return 0;
}
 
  
 /*  callback to process control commands received from frontend */
int websrv_scope_callback_set_params (const struct _u_request * request, struct _u_response * response, void * user_data) {
  websrv_dump_request("scope set params ", request);
	 json_error_t jserr;
	 json_t* jsbody = ulfius_get_json_body_request (request, &jserr);
     int httpstatus=404;
	 if (jsbody == NULL) {
	   LOG_W(UTIL,"cannot find json body in %s %s\n",request->http_url, jserr.text );
       httpstatus=400;	 
	 } else {
	   websrv_printjson("websrv_scope_callback_set_params: ",jsbody);
         json_t *J=json_object_get(jsbody, "name");
         const char *vname=json_string_value(J);
         J=json_object_get(jsbody, "value"); 
 		 const char *vval=json_string_value(J);                
         if(strcmp(vname,"startstop") == 0) {
			 if( strcmp(vval,"start") == 0) {
				if ( scope_params.statusmask & SCOPE_STATUSMASK_AVAILABLE) {
                  scope_params.statusmask |= SCOPE_STATUSMASK_STARTED;
                  scope_params.selectedTarget=0; // 1 UE to be received from GUI (for xNB scope's 
                 }
				 httpstatus=200;
			 } else if( strcmp(vval,"stop") == 0) {
                 scope_params.statusmask &= ~SCOPE_STATUSMASK_STARTED;				 
				 httpstatus=200;
			 } else {
				LOG_W(UTIL,"invalid startstop command value: %s\n",vval);
				httpstatus=400;
			 }		 
		 } else if (strcmp(vname,"refrate") == 0) {
           scope_params.refrate = (uint32_t)strtol(vval,NULL,10);
           httpstatus=200;
		 } else if (strcmp(vname,"enabled") == 0) {
           J=json_object_get(jsbody, "graphid"); 
           const int gid = json_integer_value(J); 
           OAI_phy_scope_t *sp = (OAI_phy_scope_t *)scope_params.scopeform;  
           sp->graph[gid].enabled = (strcmp(vval,"true")==0)?true:false; 
           httpstatus=200;
		 } else if (strcmp(vname,"xmin") == 0) {
           scope_params.xmin = strtol(vval,NULL,10);
           httpstatus=200;   
		 } else if (strcmp(vname,"xmax") == 0) {
           scope_params.xmax = strtol(vval,NULL,10);
           httpstatus=200;
		 } else if (strcmp(vname,"ymin") == 0) {
           scope_params.ymin = strtol(vval,NULL,10);
           httpstatus=200;
 		 } else if (strcmp(vname,"ymax") == 0) {
           scope_params.ymax = strtol(vval,NULL,10);
           httpstatus=200; 
 		 } else if (strcmp(vname,"llrxmax") == 0) {
           scope_params.llrxmax = strtol(vval,NULL,10);
           httpstatus=200;   
 		 } else if (strcmp(vname,"llrxmin") == 0) {
           scope_params.llrxmin = strtol(vval,NULL,10);
           httpstatus=200;                                 
		 } else if (strcmp(vname,"TargetSelect") == 0) {
           scope_params.selectedTarget=strtol(vval,NULL,10);
           httpstatus=200;   
		 } else if (strcmp(vname,"DataAck") == 0) {
			 if (strcasecmp(vval,"true")==0)
                scope_params.statusmask |= SCOPE_STATUSMASK_DATAACK;
             else
                scope_params.statusmask &= (~SCOPE_STATUSMASK_DATAACK);
           httpstatus=200;
		 } else if (strcmp(vname,"llrythresh") == 0) {
                scope_params.llr_ythresh = strtol(vval,NULL,10);
           httpstatus=200;                                        			                 	                       			                 			 
		 } else {
               httpstatus=500;
               LOG_W(UTIL,"Unknown scope command: %s\n",vname );
         }
     } //sbody
  ulfius_set_empty_body_response(response, httpstatus);
  return U_CALLBACK_COMPLETE;
}
 
int websrv_scope_callback_get_desc (const struct _u_request * request, struct _u_response * response, void * user_data) {
  websrv_dump_request("scope get desc ", request);
  json_t *jgraph = json_array();
  char gtype[20];
  char stitle[64];
  
    scope_params.statusmask &= ~SCOPE_STATUSMASK_STARTED;
    if (IS_SOFTMODEM_DOSCOPE | IS_SOFTMODEM_ENB_BIT | IS_SOFTMODEM_4GUE_BIT) {
	  strcpy(stitle,"none");  
	} else {	  
	  if (IS_SOFTMODEM_GNB_BIT) {			 
		scopedata.ru=RC.ru[0];
		scopedata.gNB=RC.gNB[0];		 
		scope_params.scopeform = create_phy_scope_gnb();
		scope_params.statusmask |= SCOPE_STATUSMASK_AVAILABLE;
		strcpy(stitle,"gNB"); 
	  } else if (IS_SOFTMODEM_5GUE_BIT) {
		scope_params.scopedata = PHY_vars_UE_g[0][0] ;
		nrUEinitScope(PHY_vars_UE_g[0][0]);
		scope_params.scopeform = create_phy_scope_nrue(scope_params.selectedTarget);
		scope_params.statusmask |= SCOPE_STATUSMASK_AVAILABLE;
		strcpy(stitle,"5GUE");
	  } else {
        LOG_I(UTIL,"[websrv] SoftScope web interface  not implemented for this softmodem\n");
        strcpy(stitle,"none");    	  
	  }
	}  		               
  OAI_phy_scope_t *sp = (OAI_phy_scope_t *)scope_params.scopeform;
  if(sp != NULL && (scope_params.statusmask & SCOPE_STATUSMASK_AVAILABLE))
    for (int i=0; sp->graph[i].graph != NULL ; i++) {
	  json_t *agraph =NULL;
	  switch (sp->graph[i].chartid )  {
		case SCOPEMSG_DATAID_IQ:
	      strcpy(gtype,"IQs");	  
          agraph=json_pack("{s:s,s:s,s:i,s:i,s:i,s:i}","title",sp->graph[i].graph->label,"type",gtype,
                           "id", sp->graph[i].datasetid,"srvidx",i,"w", sp->graph[i].w,"h",sp->graph[i].h);
          break;
		case SCOPEMSG_DATAID_LLR:
	      strcpy(gtype,"LLR");	  
          agraph=json_pack("{s:s,s:s,s:i,s:i,s:i,s:i}","title",sp->graph[i].graph->label,"type",gtype,
                           "id", sp->graph[i].datasetid,"srvidx",i,"w", sp->graph[i].w,"h",sp->graph[i].h);
          break;  
		case SCOPEMSG_DATAID_WF:
	      strcpy(gtype,"WF");	  
          agraph=json_pack("{s:s,s:s,s:i,s:i,s:i,s:i}","title",sp->graph[i].graph->label,"type",gtype,
                           "id", sp->graph[i].datasetid,"srvidx",i,"w", sp->graph[i].w,"h",sp->graph[i].h);
          break;                   
        default:
          break;       
      }
      if (agraph != NULL)
        json_array_append_new(jgraph,agraph);
    }
  json_t *jbody = json_pack("{s:s,s:o}","title",stitle,"graphs",jgraph);
  websrv_jbody(response,jbody);
  return U_CALLBACK_COMPLETE;
}
 
void websrv_init_scope(websrv_params_t *websrvparams) {
  int (* callback_functions_scope[3])(const struct _u_request * request, 
                                      struct _u_response * response,
                                      void * user_data) ={websrv_callback_okset_softmodem_cmdvar,websrv_scope_callback_set_params,websrv_scope_callback_get_desc};
  char *http_methods[3]={"OPTIONS","POST","GET"};
  websrvparams_ptr=websrvparams;
  websrv_add_endpoint(http_methods,3,"oaisoftmodem","scopectrl" ,callback_functions_scope, websrvparams);

 }

websrv_scope_params_t *websrv_scope_getparams(void) {
	return &scope_params;
}
