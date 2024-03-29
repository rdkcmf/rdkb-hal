/************************************************************************************
  If not stated otherwise in this file or this component's Licenses.txt file the
  following copyright and licenses apply:

  Copyright 2018 RDK Management

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
**************************************************************************/
/**********************************************************************

    module: vlan_hal.c

        For CCSP Component:  VLAN_Provisioning_and_management

    ---------------------------------------------------------------

    description:

        This sample implementation file gives the function definitions used for the RDK-Broadband 
        VLAN abstraction layer

        NOTE:
        THIS VERSION IS AN EARLY DRAFT INTENDED TO GET COMMENTS FROM COMCAST.
        TESTING HAS NOT YET BEEN COMPLETED.  
       
    ---------------------------------------------------------------

    environment:

       This HAL layer is intended to support VLAN drivers 
       through the System Calls.  

    ---------------------------------------------------------------

    author:

	zhicheng_qiu@cable.comcast.com
	
**********************************************************************/

#include "vlan_hal.h"
#include "safec_lib.h"


vlan_vlanidconfiguration_t *gpvlan_Config_Head = NULL;

/********************************************************************
 *
 *  Begining of VLAN HAL function definitions
 *
*********************************************************************/

//This HAL is used to creat an new vlan group, and assig default vlanID. 
// If success, return 0
// If group is already exist and has expected vlanID, this HAL return 0
int vlan_hal_addGroup(const char *groupName, const char *default_vlanID)
{
    char cmdBuff[128] = { 0 };
    errno_t rc = -1;

	printf("%s - Entry\n",__FUNCTION__);

	/* Prevalidation for received params */
	if( NULL == groupName )	
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return RETURN_ERR;
	}

	/* 
	* Check whether group existing or not. if existing neglet the system 
	* calls without error 
	*/
	if( 1 == _is_this_group_available_in_linux_bridge( groupName ) )
	{
		printf("%s - Already Exists. Grp Name[%s]\n",
								__FUNCTION__,
								groupName );
		return RETURN_OK;
	}

	/* Add new group on bridge */
    rc = sprintf_s(cmdBuff, sizeof(cmdBuff), "brctl addbr %s", groupName);
    if(rc < EOK)
    {
        ERR_CHK(rc);
    }
    system(cmdBuff);

    /* 
	 * If valid default_vlanid received means we need to store that ID and 
	 * corresponding group into global link   
	 */
	if( NULL != default_vlanID )
	{
		/* insert the default VLANID with groupname into link */
		insert_VLAN_ConfigEntry(groupName,default_vlanID);
	}

	printf("%s - Exit\n",__FUNCTION__);
	
	return RETURN_OK;
}

//This HAL is used to delete existing vlan group, and delete correspond interface association. 
// If success, return 0
// If group is not exist, return 0,
int vlan_hal_delGroup(const char *groupName)
{
    char cmdBuff[128] = { 0 };
    errno_t rc = -1;

	printf("%s - Entry\n",__FUNCTION__);

	/* Prevalidation for received params */
	if( NULL == groupName )	
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return RETURN_ERR;
	}

	/* 
	* Check whether group existing or not. if not existing neglet the system 
	* calls without error 
	*/
	if( 0 == _is_this_group_available_in_linux_bridge( groupName ) )
	{
		printf("%s - Not Exists. Grp Name[%s]\n",
								__FUNCTION__,
								groupName );
		return RETURN_OK;
	}

	/*
	* Before removing bridge from network delete all the bridge interfaces from 
	* group and these are must be moved to "down state" by below system call 
	*/
	vlan_hal_delete_all_Interfaces(groupName);

	rc = sprintf_s(cmdBuff, 
		     sizeof(cmdBuff), 
		     "ip link set %s down", 
		     groupName);
    if(rc < EOK)
    {
        ERR_CHK(rc);
    }

	system(cmdBuff);

	/* Delete already existing group on bridge */

    rc = sprintf_s(cmdBuff, 
		     sizeof(cmdBuff), 
		     "brctl delbr %s", 
		     groupName);
    if(rc < EOK)
    {
        ERR_CHK(rc);
    }


    system(cmdBuff);

	/* Delete Group Entry from Link */
	delete_VLAN_ConfigEntry( groupName );

	printf("%s - Exit\n",__FUNCTION__);
	
	return RETURN_OK;
}

//This HAL is used to add interface to existing vlan group, and assign the vlanID. 
// If success, return 0
// If group is not exist, return -1
// If interface is already in group, and has expected vlan ID, return 0
int vlan_hal_addInterface(const char *groupName, const char *ifName, const char *vlanID)
{
    char cmdBuff[128] = { 0 };
	char TempvlanID[VLAN_HAL_MAX_VLANID_TEXT_LENGTH]= { 0 };
	errno_t rc = -1;

	printf("%s - Entry\n",__FUNCTION__);

	/* Prevalidation for received params */
	if( ( NULL == groupName )|| \
		( NULL == ifName ) 	
	   )
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return RETURN_ERR;
	}

	/* 
	* Check whether group existing or not. if not existing neglet the system 
	* calls without error 
	*/
	if( 0 == _is_this_group_available_in_linux_bridge( groupName ) )
	{
		printf("%s - Grp Not Exists. Grp Name[%s] Inf Name[%s]\n",
								__FUNCTION__,
								groupName,
								ifName );
		return RETURN_ERR;
	}

    /* 
	 * If invalid vlanid received means we need to get the vlanid for 
	 * corresponding groupname from global link. Otherwise use received vlanid 
	 * further
	 */
	if( NULL == vlanID )
	{
		/* get the default VLANID based on groupname from link */
	   if ( RETURN_ERR == get_vlanId_for_GroupName(groupName,TempvlanID) )
	   {
			printf("%s - VLANID Not Exists in Link. Grp Name[%s] Inf Name[%s]\n",
									__FUNCTION__,
									groupName,
									ifName );

		  return RETURN_ERR;
	   }
	}
	else
	{
	   rc = strcpy_s( TempvlanID, sizeof( TempvlanID ), vlanID );
	   ERR_CHK(rc);
	}

	/* 
	* Check whether interface existing or not. if existing neglet the system 
	* calls without error. Because linux doesn't allow duplicate entry in 
	* between bridge
	*/
	if ( 1 == _is_this_interface_available_in_linux_bridge( ifName, TempvlanID ) )
	{
		printf("%s - Inf Already Exists. Inf Name[%s] VlanID[%s]\n",
								__FUNCTION__,
								ifName,
								TempvlanID );
		return RETURN_OK;
	}

	/* Add new device interface into bridge */
	rc = sprintf_s(cmdBuff, 
			 sizeof(cmdBuff), 
			 "vconfig add %s %s; brctl addif %s %s.%s", 
			 ifName,
			 TempvlanID,
			 groupName,
			 ifName,
			 TempvlanID);
	if(rc < EOK)
	{
		ERR_CHK(rc);
	}
	system(cmdBuff);
	
	printf("%s - Exit\n",__FUNCTION__);

	return RETURN_OK;
}

//This HAL is used to deassociate existing interface from group. 
// If success, return 0
// If interface is not exist, return 0,
int vlan_hal_delInterface(const char *groupName, const char *ifName, const char *vlanID)
{
	char cmdBuff[128] = { 0 };
	char TempvlanID[VLAN_HAL_MAX_VLANID_TEXT_LENGTH]= { 0 };
	errno_t rc = -1;

	printf("%s - Entry\n",__FUNCTION__);

	/* Prevalidation for received params */
	if( ( NULL == groupName )|| \
		( NULL == ifName ) 	
	   )
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return RETURN_ERR;
	}

	/* 
	* Check whether group existing or not. if not existing neglet the system 
	* calls without error 
	*/
	if( 0 == _is_this_group_available_in_linux_bridge( groupName ) )
	{
		printf("%s - Grp Not Exists. Grp Name[%s] Inf Name[%s]\n",
								__FUNCTION__,
								groupName,
								ifName );
		return RETURN_ERR;
	}
	
    /* 
	 * If invalid vlanid received means we need to get the vlanid for 
	 * corresponding groupname from global link. Otherwise use received vlanid 
	 * further
	 */
	if( NULL == vlanID )
	{
	   /* get the default VLANID based on groupname from link */
	   if ( RETURN_ERR == get_vlanId_for_GroupName(groupName,TempvlanID) )
	   {
			printf("%s - VLANID Not Exists in Link. Grp Name[%s] Inf Name[%s]\n",
									__FUNCTION__,
									groupName,
									ifName );

		  return RETURN_ERR;
	   }
	}
	else
	{
	   rc = strcpy_s( TempvlanID, sizeof( TempvlanID ), vlanID );
	   ERR_CHK(rc);
	}

	/* 
	* Check whether group and interface existing or not. if not existing neglet the system 
	* calls without error 
	*/
	if ( 0 == _is_this_interface_available_in_given_linux_bridge( ifName, groupName, TempvlanID ) )
	{
		printf("%s - Inf Not Exists. Grp Name[%s] Inf Name[%s] VlanID[%s]\n",
								__FUNCTION__,
								groupName,
								ifName,
								TempvlanID );
		return RETURN_OK;
	}

	/* Delete already existing device interface from bridge */
	rc = memset_s(cmdBuff, sizeof( cmdBuff ),0, sizeof(cmdBuff));
	ERR_CHK(rc);
	rc = sprintf_s(cmdBuff, 
			 sizeof(cmdBuff), 
			 "vconfig rem %s.%s; brctl delif %s %s", 
			 ifName,
			 TempvlanID,
			 groupName,
			 ifName);
    if(rc < EOK)
    {
        ERR_CHK(rc);
    }

	system(cmdBuff);
	
	printf("%s - Exit\n",__FUNCTION__);

	return RETURN_OK;
}

//This HAL is used dump the group setting, for debug purpose
int vlan_hal_printGroup(const char *groupName)
{
	char cmd[128];
	errno_t rc = -1;

	printf("%s - Entry\n",__FUNCTION__);

	/* Prevalidation for received params */
	if( NULL == groupName )
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return RETURN_ERR;
	}

	rc = sprintf_s(cmd, sizeof(cmd), "brctl show %s", groupName);
	if(rc < EOK)
	{
		ERR_CHK(rc);
	}
 	system(cmd);

	/* memset(cmd,0,sizeof(cmd));
	    sprintf(cmd, "brctl showmacs %s", groupName);
	    system(cmd);

	    memset(cmd,0,sizeof(cmd));
	    sprintf(cmd, "brctl showstp %s", groupName);
	    system(cmd); 
	 */
	
	printf("%s - Exit\n",__FUNCTION__);

	return RETURN_OK;
}

//This HAL is used dump all group setting. 
int vlan_hal_printAllGroup( void )
{
	char cmd[128];
	errno_t rc = -1;

	printf("%s - Entry\n",__FUNCTION__);

	rc = strcpy_s(cmd, sizeof(cmd) , "brctl show");
	ERR_CHK(rc);
    system(cmd);

	printf("%s - Exit\n",__FUNCTION__);
	
	return RETURN_OK;
}

//This HAL is used to deassociate all existing interface from group. 
int vlan_hal_delete_all_Interfaces(const char *groupName)
{
    FILE  *fp;
	char   cmdBuff[128] = { 0 };
	errno_t rc = -1;

	printf("%s - Entry\n",__FUNCTION__);

	/* Prevalidation for received params */
	if( NULL == groupName )	
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return RETURN_ERR;
	}

	rc = sprintf_s(cmdBuff, 
			 sizeof(cmdBuff), 
			 "brctl show %s | awk '{ print $NF }' | tail +2", 
			 groupName);
    if(rc < EOK)
    {
        ERR_CHK(rc);
    }

    fp = popen(cmdBuff, "r");

    if(fp)
    {
		char  buf[128] = { 0 };

		while(fgets(buf, VLAN_HAL_MAX_LINE_BUFFER_LENGTH, fp) != NULL)
		{
			char ifName[VLAN_HAL_MAX_INTERFACE_NAME_TEXT_LENGTH] = { 0 }, 
				 vlanID[VLAN_HAL_MAX_VLANID_TEXT_LENGTH] 		 = { 0 };

			/* Split out interface name and vlanid from buffer */
			sscanf(buf,"%[^.].%s",ifName,vlanID);
			
			printf("%s - Del Inf, Grp Name[%s] Inf[%s] VlanID[%s]\n",
							__FUNCTION__, 
							groupName,
							ifName,
							vlanID);

			/* Delete interface corresponding group */
			vlan_hal_delInterface( groupName, ifName, vlanID );
		}

        pclose(fp);        
    }
	
	printf("%s - Exit\n",__FUNCTION__);

	return RETURN_OK;
}

//This HAL utility is used identify given bridge available in linux bridge
int _is_this_group_available_in_linux_bridge(char * br_name)
{
    char buf[512] = {0};
    char cmd[128] = {0};
    errno_t rc = -1;

	/* Prevalidation for received params */
	if( NULL == br_name )
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return 0;
	}

    rc = sprintf_s(cmd, sizeof(cmd) , "brctl show | grep -w %s", br_name );
    if(rc < EOK)
    {
        ERR_CHK(rc);
    }
    _get_shell_outputbuffer(cmd, buf, sizeof(buf));

    if (strstr(buf, br_name))
        return 1;
    else
        return 0;
}

//This HAL utility is used identify given interface available in anyone of linux bridge
int _is_this_interface_available_in_linux_bridge(char * if_name, char *vlanID)
{
    FILE  *fp; 
    char   buf[512] 	= { 0 };
    char   cmdBuff[128] = { 0 };
    BOOL   bFound 	    = FALSE;
    errno_t rc = -1;

/* Prevalidation for received params */
   if( ( NULL == if_name )|| ( NULL == vlanID ) )
	   
   {
	printf("%s - Invalid Params\n",__FUNCTION__);
	return 0;
   }

    rc = strcpy_s(cmdBuff, sizeof(cmdBuff),"brctl show | awk '{ print $NF }' | tail +2" );
    ERR_CHK(rc);

   fp = popen(cmdBuff, "r");

    if(fp)
    {
		char  buf[128] = { 0 };

		while(fgets(buf, VLAN_HAL_MAX_LINE_BUFFER_LENGTH, fp) != NULL)
		{
			char TempifName[VLAN_HAL_MAX_INTERFACE_NAME_TEXT_LENGTH] = { 0 }, 
				 TempvlanID[VLAN_HAL_MAX_VLANID_TEXT_LENGTH] 		 = { 0 };

			/* Split out interface name and vlanid from buffer */
			sscanf(buf,"%[^.].%s",TempifName,TempvlanID);
			
			printf("%s - Search Inf, For[Inf[%s] VlanID[%s]] Recvd[Inf[%s] VlanID[%s]]\n",
							__FUNCTION__, 
							TempifName,
							TempvlanID,
							if_name,
							vlanID);
			/* 
			  * Search whether give Interface and VlanID available or not. If 
			  * available set found flag = 1  
			  */
			if( ( 0 == strcmp( TempifName, if_name ) ) && \
				( 0 == strcmp( TempvlanID, vlanID ) ) 
			  )
			{
				bFound = TRUE;
				break;
			}
		}

        pclose(fp);        
    }

    if ( TRUE == bFound )
        return 1;
    else
        return 0;
}

//This HAL utility is used identify given interface available in given bridge
int _is_this_interface_available_in_given_linux_bridge(char * if_name, char * br_name,char *vlanID)
{
    FILE  *fp; 
    char   buf[512] 	= { 0 };
    char   cmdBuff[128] = { 0 };
	BOOL   bFound 	    = FALSE;
	errno_t rc = -1;

	/* Prevalidation for received params */
	if( ( NULL == br_name )|| \
		( NULL == if_name )|| \
		( NULL == vlanID )
	   )
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return 0;
	}

	rc = sprintf_s(cmdBuff, 
			 sizeof(cmdBuff), 
			 "brctl show %s | awk '{ print $NF }' | tail +2", 
			 br_name);
	if(rc < EOK)
	{
		ERR_CHK(rc);
	}

    fp = popen(cmdBuff, "r");

    if(fp)
    {
		char  buf[128] = { 0 };

		while(fgets(buf, VLAN_HAL_MAX_LINE_BUFFER_LENGTH, fp) != NULL)
		{
			char TempifName[VLAN_HAL_MAX_INTERFACE_NAME_TEXT_LENGTH] = { 0 }, 
				 TempvlanID[VLAN_HAL_MAX_VLANID_TEXT_LENGTH] 		 = { 0 };

			/* Split out interface name and vlanid from buffer */
			sscanf(buf,"%[^.].%s",TempifName,TempvlanID);
			
			printf("%s - Search Inf, For[Inf[%s] VlanID[%s]] Recvd[Grp Name[%s] Inf[%s] VlanID[%s]]\n",
							__FUNCTION__, 
							TempifName,
							TempvlanID,
							br_name,
							if_name,
							vlanID);
			/* 
			  * Search whether give Interface and VlanID available or not. If 
			  * available set found flag = 1  
			  */
			if( ( 0 == strcmp( TempifName, if_name ) ) && \
				( 0 == strcmp( TempvlanID, vlanID ) ) 
			  )
			{
				bFound = TRUE;
				break;
			}
		}

        pclose(fp);        
    }

    if ( TRUE == bFound )
        return 1;
    else
        return 0;
}


//This HAL utility is used to execute and get the buffer from shell output based on given command
void _get_shell_outputbuffer(char * cmd, char * out, int len)
{
    FILE * fp;
    char   buf[512] = { 0 };
    errno_t rc = -1;

    fp = popen(cmd, "r");

    if (fp)
    {
		char line_buff[128] = { 0 };

	   /*
		* Read full buffer of shell command
		* 
		* Note: 
		* ----
		* 1. Dont use fgetc() -It doesn't end up EOF
		* 2. Dont use fread() -It leads to crash the process
		*/
		while( fgets(line_buff, VLAN_HAL_MAX_LINE_BUFFER_LENGTH, fp) != NULL )
		{
			rc = strcat_s( buf, sizeof(buf) ,line_buff );
			ERR_CHK(rc);
		}

		rc = strncpy_s( out, len , buf, len - 1 );
		ERR_CHK(rc);

        pclose(fp);        
    }
}

//This HAL utility is used store the VLAN ID, Group Name configuration into link
int insert_VLAN_ConfigEntry(char *groupName, char *vlanID)
{
	vlan_vlanidconfiguration_t *pvlan_Config_Temp = NULL;
	errno_t rc = -1;

	if( ( NULL == groupName ) || \
		( NULL == vlanID )
	  )
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return RETURN_ERR;
	}

	/* Allocate memory for VLAN Config Struct */
	pvlan_Config_Temp = ( vlan_vlanidconfiguration_t* )malloc( sizeof( vlan_vlanidconfiguration_t ) );

	if( NULL == pvlan_Config_Temp )
	{
		printf("%s - Memory Allocate Fails\n",__FUNCTION__);
		return RETURN_ERR;
	}

	rc = memset_s( pvlan_Config_Temp, sizeof( vlan_vlanidconfiguration_t ), 0, sizeof( vlan_vlanidconfiguration_t ) );
	ERR_CHK(rc);
	rc = strcpy_s( pvlan_Config_Temp->groupName, sizeof( pvlan_Config_Temp->groupName ) , groupName);
	ERR_CHK(rc);
	rc = strcpy_s( pvlan_Config_Temp->vlanID, sizeof( pvlan_Config_Temp->vlanID ) , vlanID );
	ERR_CHK(rc);
	pvlan_Config_Temp->nextlink = NULL;
	
    /* 
        * Insert this configuration into link
        */
	if( NULL == gpvlan_Config_Head ) 
	{
		gpvlan_Config_Head  = pvlan_Config_Temp;
	}
	else
	{
		vlan_vlanidconfiguration_t *pvlan_Config_curr = NULL;

		pvlan_Config_curr = gpvlan_Config_Head;

		while( NULL != pvlan_Config_curr->nextlink )
		{
			pvlan_Config_curr = pvlan_Config_curr->nextlink;
		}

		pvlan_Config_curr->nextlink = pvlan_Config_Temp;
	}

	return RETURN_OK;
}

//This HAL utility is used delete the VLAN ID, Group Name configuration from link
int delete_VLAN_ConfigEntry(char *groupName)
{
	vlan_vlanidconfiguration_t *pvlan_Config_prev = NULL,
							   *pvlan_Config_curr = NULL;

	if( NULL == groupName ) 
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return RETURN_ERR;
	}

	/* Assign the Head Configuration into pre and curr Configuration */
	pvlan_Config_prev = NULL;
	pvlan_Config_curr = gpvlan_Config_Head;	

	/* Traverse the link while getting corresponding link */
	while( NULL != pvlan_Config_curr )
	{
		if( 0 == strcmp( pvlan_Config_curr->groupName, groupName ) )
		{
		  if( NULL == pvlan_Config_prev )
		  {
			 gpvlan_Config_Head = pvlan_Config_curr->nextlink;
		  }
		  else
		  {
			 pvlan_Config_prev->nextlink = pvlan_Config_curr->nextlink;
		  }

		  free( pvlan_Config_curr );
		  pvlan_Config_curr = NULL;

		  break;
		}

		pvlan_Config_prev = pvlan_Config_curr;
		pvlan_Config_curr = pvlan_Config_curr->nextlink;
	}

	return RETURN_OK;
}

//This HAL utility is used get the VLAN ID for corresponding Group Name from link
int get_vlanId_for_GroupName(const char *groupName, char *vlanID)
{
	vlan_vlanidconfiguration_t *pvlan_Config_temp = NULL;
	BOOL					    bFound 			  = FALSE;
	errno_t rc = -1;

	if( ( NULL == groupName ) || \
		( NULL == vlanID )
	  )
	{
		printf("%s - Invalid Params\n",__FUNCTION__);
		return RETURN_ERR;
	}

	/* Assign the Head Configuration into pre and curr Configuration */
	pvlan_Config_temp = gpvlan_Config_Head;	

	/* Traverse the link while getting corresponding link */
	while( NULL != pvlan_Config_temp )
	{
		/*
		 * Find and Copy the VLANID for corresponding group then break the loop
		 */
		if( 0 == strcmp( pvlan_Config_temp->groupName, groupName ) )
		{
		  rc = memcpy_s( vlanID, 32,
		  		  pvlan_Config_temp->vlanID, 
		  		  sizeof( pvlan_Config_temp->vlanID ) - 1 );
		  ERR_CHK(rc);

		  bFound = TRUE;
		  break;
		}

		pvlan_Config_temp = pvlan_Config_temp->nextlink;
	}

	/* 
	 * If VLANID found case need to return success otherwise return error code 
 	 * to caller 
	 */
	if( bFound )
	{
		return RETURN_OK;
	}
	
	return RETURN_ERR;
}

//This HAL utility is used list all VLAN ID for corresponding Group Name from link
int print_all_vlanId_Configuration(void)
{
	vlan_vlanidconfiguration_t *pvlan_Config_temp = NULL;

	/* Assign the Head Configuration into pre and curr Configuration */
	pvlan_Config_temp = gpvlan_Config_Head;	

	printf("-----------------------------\n");
	printf("- GroupName	|	VLANID -\n");
	printf("-----------------------------\n");
	
	/* Traverse the full link to print vlan configuration */
	while( NULL != pvlan_Config_temp )
	{
		printf("  [%s]	|	[%s]\n",
				pvlan_Config_temp->groupName, 
				pvlan_Config_temp->vlanID );
		
		pvlan_Config_temp = pvlan_Config_temp->nextlink;
	}

	return RETURN_OK;
}

/*********************************************************************************************
 *
 *  End of VLAN HAL function definitions
 *
**********************************************************************************************/
