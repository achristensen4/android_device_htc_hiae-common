/*
Copyright (c) 2013, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided
with the distribution.

* Neither the name of The Linux Foundation nor the names of its
contributors may be used to endorse or promote products derived
from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*!
	@file
	IPACM_Lan.cpp

	@brief
	This file implements the LAN iface functionality.

	@Author
	Skylar Chang

*/
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "IPACM_Netlink.h"
#include "IPACM_Lan.h"
#include "IPACM_Wan.h"
#include "IPACM_IfaceManager.h"
#include "linux/rmnet_ipa_fd_ioctl.h"
#include "linux/ipa_qmi_service_v01.h"
#include "linux/msm_ipa.h"
#include "IPACM_ConntrackListener.h"
#include <sys/ioctl.h>
#include <fcntl.h>

bool IPACM_Lan::odu_up = false;

ipa_hdr_l2_type IPACM_Lan::lan_hdr_type = IPA_HDR_L2_NONE;
ipa_hdr_l2_type IPACM_Lan::wlan_hdr_type = IPA_HDR_L2_NONE;

uint32_t IPACM_Lan::usb_hdr_template_hdl = 0;
uint32_t IPACM_Lan::wlan_hdr_template_hdl = 0;
uint32_t IPACM_Lan::cpe_hdr_template_hdl = 0;

hdr_proc_ctx_info IPACM_Lan::lan_to_wlan_hdr_proc_ctx;
hdr_proc_ctx_info IPACM_Lan::wlan_to_usb_hdr_proc_ctx;
hdr_proc_ctx_info IPACM_Lan::wlan_to_cpe_hdr_proc_ctx;
hdr_proc_ctx_info IPACM_Lan::wlan_to_wlan_hdr_proc_ctx;
hdr_proc_ctx_info IPACM_Lan::usb_to_cpe_hdr_proc_ctx;
hdr_proc_ctx_info IPACM_Lan::cpe_to_usb_hdr_proc_ctx;

eth_bridge_subnet_client_info IPACM_Lan::eth_bridge_wlan_client[IPA_LAN_TO_LAN_MAX_WLAN_CLIENT];
eth_bridge_subnet_client_info IPACM_Lan::eth_bridge_lan_client[IPA_LAN_TO_LAN_MAX_LAN_CLIENT];

int IPACM_Lan::num_wlan_client = 0;
int IPACM_Lan::num_lan_client = 0;
bool IPACM_Lan::is_usb_up = false;
bool IPACM_Lan::is_cpe_up = false;

IPACM_Lan::IPACM_Lan(int iface_index) : IPACM_Iface(iface_index)
{
	num_eth_client = 0;
	header_name_count = 0;
	ipv6_set = 0;
	ipv4_header_set = false;
	ipv6_header_set = false;
	odu_route_rule_v4_hdl = NULL;
	odu_route_rule_v6_hdl = NULL;
	eth_client = NULL;
	int m_fd_odu, ret = IPACM_SUCCESS;
	uint32_t* hdr_template_ptr;

	Nat_App = NatApp::GetInstance();
	if (Nat_App == NULL)
	{
		IPACMERR("unable to get Nat App instance \n");
		return;
	}

	/* support eth multiple clients */
	if(iface_query != NULL)
	{
		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat != WLAN_IF)
		{
			eth_client_len = (sizeof(ipa_eth_client)) + (iface_query->num_tx_props * sizeof(eth_client_rt_hdl));
			eth_client = (ipa_eth_client *)calloc(IPA_MAX_NUM_ETH_CLIENTS, eth_client_len);
			if (eth_client == NULL)
			{
				IPACMERR("unable to allocate memory\n");
				return;
			}
		}

		IPACMDBG_H(" IPACM->IPACM_Lan(%d) constructor: Tx:%d Rx:%d \n", ipa_if_num,
					 iface_query->num_tx_props, iface_query->num_rx_props);
#ifdef FEATURE_ETH_BRIDGE_LE
		if((IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF
			|| IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF) && tx_prop != NULL)
		{
			if (IPACM_Lan::lan_hdr_type != IPA_HDR_L2_NONE && tx_prop->tx[0].hdr_l2_type != IPACM_Lan::lan_hdr_type)
			{
				IPACMERR("The LAN header format is not consistent! Now header format is %d.\n", tx_prop->tx[0].hdr_l2_type);
			}
			else
			{
				if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
				{
					hdr_template_ptr = &IPACM_Lan::usb_hdr_template_hdl;
					IPACM_Lan::is_usb_up = true;
				}
				else //else it is ODU_IF (cpe)
				{
					hdr_template_ptr = &IPACM_Lan::cpe_hdr_template_hdl;
					IPACM_Lan::is_cpe_up = true;
				}

				if(eth_bridge_get_hdr_template_hdl(hdr_template_ptr) == IPACM_FAILURE)
				{
					IPACMERR("Failed to setup lan hdr template.\n");
				}
				else
				{
					IPACM_Lan::lan_hdr_type = tx_prop->tx[0].hdr_l2_type;
					IPACMDBG_H("The LAN header format is %d.\n", tx_prop->tx[0].hdr_l2_type);
					add_hdr_proc_ctx();
				}
			}
		}
#endif
		/* ODU routing table initilization */
		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
		{
			odu_route_rule_v4_hdl = (uint32_t *)calloc(iface_query->num_tx_props, sizeof(uint32_t));
			odu_route_rule_v6_hdl = (uint32_t *)calloc(iface_query->num_tx_props, sizeof(uint32_t));
			if ((odu_route_rule_v4_hdl == NULL) || (odu_route_rule_v6_hdl == NULL))
			{
				IPACMERR("unable to allocate memory\n");
				return;
			}
		}
	}

	num_wan_ul_fl_rule_v4 = 0;
	num_wan_ul_fl_rule_v6 = 0;

	memset(wan_ul_fl_rule_hdl_v4, 0, MAX_WAN_UL_FILTER_RULES * sizeof(uint32_t));
	memset(wan_ul_fl_rule_hdl_v6, 0, MAX_WAN_UL_FILTER_RULES * sizeof(uint32_t));

	memset(lan2lan_flt_rule_hdl_v4, 0, MAX_OFFLOAD_PAIR * sizeof(lan2lan_flt_rule_hdl));
	num_lan2lan_flt_rule_v4 = 0;

	memset(lan2lan_flt_rule_hdl_v6, 0, MAX_OFFLOAD_PAIR * sizeof(lan2lan_flt_rule_hdl));
	num_lan2lan_flt_rule_v6 = 0;

	memset(lan2lan_hdr_hdl_v4, 0, MAX_OFFLOAD_PAIR*sizeof(lan2lan_hdr_hdl));
	memset(lan2lan_hdr_hdl_v6, 0, MAX_OFFLOAD_PAIR*sizeof(lan2lan_hdr_hdl));

	memset(lan_client_flt_rule_hdl_v4, 0, IPA_LAN_TO_LAN_MAX_LAN_CLIENT * sizeof(lan2lan_flt_rule_hdl));
	memset(lan_client_flt_rule_hdl_v6, 0, IPA_LAN_TO_LAN_MAX_LAN_CLIENT * sizeof(lan2lan_flt_rule_hdl));
	memset(wlan_client_flt_rule_hdl_v4, 0, IPA_LAN_TO_LAN_MAX_WLAN_CLIENT * sizeof(lan2lan_flt_rule_hdl));
	memset(wlan_client_flt_rule_hdl_v6, 0, IPA_LAN_TO_LAN_MAX_WLAN_CLIENT * sizeof(lan2lan_flt_rule_hdl));

	is_active = true;
	memset(ipv4_icmp_flt_rule_hdl, 0, NUM_IPV4_ICMP_FLT_RULE * sizeof(uint32_t));
	memset(tcp_ctl_flt_rule_hdl_v4, 0, NUM_TCP_CTL_FLT_RULE*sizeof(uint32_t));
	memset(tcp_ctl_flt_rule_hdl_v6, 0, NUM_TCP_CTL_FLT_RULE*sizeof(uint32_t));
	is_mode_switch = false;
	if_ipv4_subnet =0;
	memset(private_fl_rule_hdl, 0, IPA_MAX_PRIVATE_SUBNET_ENTRIES * sizeof(uint32_t));
	memset(ipv6_prefix_flt_rule_hdl, 0, NUM_IPV6_PREFIX_FLT_RULE * sizeof(uint32_t));
	memset(ipv6_icmp_flt_rule_hdl, 0, NUM_IPV6_ICMP_FLT_RULE * sizeof(uint32_t));

#ifdef FEATURE_ETH_BRIDGE_LE
	if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
	{
		exp_index_v4 = IPV4_DEFAULT_FILTERTING_RULES + IPA_LAN_TO_LAN_MAX_WLAN_CLIENT + IPA_LAN_TO_LAN_MAX_CPE_CLIENT + NUM_IPV4_ICMP_FLT_RULE + IPACM_Iface::ipacmcfg->ipa_num_private_subnet;
		exp_index_v6 = IPV6_DEFAULT_FILTERTING_RULES + IPA_LAN_TO_LAN_MAX_WLAN_CLIENT + IPA_LAN_TO_LAN_MAX_CPE_CLIENT + NUM_IPV6_ICMP_FLT_RULE + NUM_IPV6_PREFIX_FLT_RULE;
	}
	if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
	{
		exp_index_v4 = IPV4_DEFAULT_FILTERTING_RULES + IPA_LAN_TO_LAN_MAX_WLAN_CLIENT + IPA_LAN_TO_LAN_MAX_USB_CLIENT + NUM_IPV4_ICMP_FLT_RULE + IPACM_Iface::ipacmcfg->ipa_num_private_subnet;
		exp_index_v6 = IPV6_DEFAULT_FILTERTING_RULES + IPA_LAN_TO_LAN_MAX_WLAN_CLIENT + IPA_LAN_TO_LAN_MAX_USB_CLIENT + NUM_IPV6_ICMP_FLT_RULE + NUM_IPV6_PREFIX_FLT_RULE;
	}
#else
#ifdef CT_OPT
	exp_index_v4 = IPV4_DEFAULT_FILTERTING_RULES + MAX_OFFLOAD_PAIR + NUM_TCP_CTL_FLT_RULE + NUM_IPV4_ICMP_FLT_RULE +IPACM_Iface::ipacmcfg->ipa_num_private_subnet;
	exp_index_v6 = IPV6_DEFAULT_FILTERTING_RULES + NUM_TCP_CTL_FLT_RULE + MAX_OFFLOAD_PAIR + NUM_IPV6_ICMP_FLT_RULE + NUM_IPV6_PREFIX_FLT_RULE;
#else
	exp_index_v4 = IPV4_DEFAULT_FILTERTING_RULES + MAX_OFFLOAD_PAIR + NUM_IPV4_ICMP_FLT_RULE + IPACM_Iface::ipacmcfg->ipa_num_private_subnet;
	exp_index_v6 = IPV6_DEFAULT_FILTERTING_RULES + MAX_OFFLOAD_PAIR + NUM_IPV6_ICMP_FLT_RULE + NUM_IPV6_PREFIX_FLT_RULE;
#endif
#ifdef FEATURE_IPA_ANDROID
	exp_index_v4 = exp_index_v4 - IPACM_Iface::ipacmcfg->ipa_num_private_subnet + IPA_MAX_PRIVATE_SUBNET_ENTRIES;
#endif
#endif

	/* ODU routing table initilization */
	if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
	{

		/* only do one time ioctl to odu-driver to infrom in router or bridge mode*/
		if (IPACM_Lan::odu_up != true)
		{
				m_fd_odu = open(IPACM_Iface::ipacmcfg->DEVICE_NAME_ODU, O_RDWR);
				if (0 == m_fd_odu)
				{
					IPACMERR("Failed opening %s.\n", IPACM_Iface::ipacmcfg->DEVICE_NAME_ODU);
					return ;
				}

				if(IPACM_Iface::ipacmcfg->ipacm_odu_router_mode == true)
				{
					ret = ioctl(m_fd_odu, ODU_BRIDGE_IOC_SET_MODE, ODU_BRIDGE_MODE_ROUTER);
					IPACM_Iface::ipacmcfg->ipacm_odu_enable = true;
				}
				else
				{
					ret = ioctl(m_fd_odu, ODU_BRIDGE_IOC_SET_MODE, ODU_BRIDGE_MODE_BRIDGE);
					IPACM_Iface::ipacmcfg->ipacm_odu_enable = true;
				}

				if (ret)
				{
					IPACMERR("Failed tell odu-driver the mode\n");
				}
				IPACMDBG("Tell odu-driver in router-mode(%d)\n", IPACM_Iface::ipacmcfg->ipacm_odu_router_mode);
				IPACMDBG_H("odu is up: odu-driver in router-mode(%d) \n", IPACM_Iface::ipacmcfg->ipacm_odu_router_mode);
				close(m_fd_odu);
				IPACM_Lan::odu_up = true;
		}
	}

	int i;
	each_client_rt_rule_count_v4 = 0;
	each_client_rt_rule_count_v6 = 0;
	if(iface_query != NULL && tx_prop != NULL)
	{
		for(i=0; i<iface_query->num_tx_props; i++)
		{
			if(tx_prop->tx[i].ip == IPA_IP_v4)
			{
				each_client_rt_rule_count_v4++;
			}
			else
			{
				each_client_rt_rule_count_v6++;
			}
		}
	}
	IPACMDBG_H("Need to add %d IPv4 and %d IPv6 routing rules for eth bridge for each client.\n", each_client_rt_rule_count_v4, each_client_rt_rule_count_v6);

	memset(eth_bridge_lan_client_flt_info, 0, IPA_LAN_TO_LAN_MAX_LAN_CLIENT * sizeof(eth_bridge_client_flt_info));
	memset(eth_bridge_wlan_client_flt_info, 0, IPA_LAN_TO_LAN_MAX_WLAN_CLIENT * sizeof(eth_bridge_client_flt_info));

	lan_client_flt_info_count = 0;
	wlan_client_flt_info_count = 0;

	eth_bridge_lan_client_rt_from_lan_info_v4 = NULL;
	eth_bridge_lan_client_rt_from_lan_info_v6 = NULL;
	eth_bridge_lan_client_rt_from_wlan_info_v4 = NULL;
	eth_bridge_lan_client_rt_from_wlan_info_v6 = NULL;
#ifdef FEATURE_ETH_BRIDGE_LE
	if(tx_prop != NULL)
	{
		client_rt_info_size_v4 = sizeof(eth_bridge_client_rt_info) + each_client_rt_rule_count_v4 * sizeof(uint32_t);
		eth_bridge_lan_client_rt_from_lan_info_v4 = (eth_bridge_client_rt_info*)calloc(IPA_LAN_TO_LAN_MAX_LAN_CLIENT, client_rt_info_size_v4);
		eth_bridge_lan_client_rt_from_wlan_info_v4 = (eth_bridge_client_rt_info*)calloc(IPA_LAN_TO_LAN_MAX_LAN_CLIENT, client_rt_info_size_v4);
		client_rt_info_size_v6 = sizeof(eth_bridge_client_rt_info) + each_client_rt_rule_count_v6 * sizeof(uint32_t);
		eth_bridge_lan_client_rt_from_lan_info_v6 = (eth_bridge_client_rt_info*)calloc(IPA_LAN_TO_LAN_MAX_WLAN_CLIENT, client_rt_info_size_v6);
		eth_bridge_lan_client_rt_from_wlan_info_v6 = (eth_bridge_client_rt_info*)calloc(IPA_LAN_TO_LAN_MAX_WLAN_CLIENT, client_rt_info_size_v6);

	}
#endif
	lan_client_rt_from_lan_info_count_v4 = 0;
	lan_client_rt_from_lan_info_count_v6 = 0;
	lan_client_rt_from_wlan_info_count_v4 = 0;
	lan_client_rt_from_wlan_info_count_v6 = 0;

#ifdef FEATURE_IPA_ANDROID
	/* set the IPA-client pipe enum */
	if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
	{
		handle_tethering_client(false, IPACM_CLIENT_USB);
	}
#endif
	return;
}

IPACM_Lan::~IPACM_Lan()
{
	IPACM_EvtDispatcher::deregistr(this);
	IPACM_IfaceManager::deregistr(this);
	return;
}


/* LAN-iface's callback function */
void IPACM_Lan::event_callback(ipa_cm_event_id event, void *param)
{
	if(is_active == false && event != IPA_LAN_DELETE_SELF)
	{
		IPACMDBG_H("The interface is no longer active, return.\n");
		return;
	}

	int ipa_interface_index;
	ipacm_ext_prop* ext_prop;
	ipacm_event_iface_up* data_wan;
	ipacm_event_iface_up_tehter* data_wan_tether;

	switch (event)
	{
	case IPA_LINK_DOWN_EVENT:
		{
			ipacm_event_data_fid *data = (ipacm_event_data_fid *)param;
			ipa_interface_index = iface_ipa_index_query(data->if_index);
			if (ipa_interface_index == ipa_if_num)
			{
				IPACMDBG_H("Received IPA_LINK_DOWN_EVENT\n");
				handle_down_evt();
				IPACM_Iface::ipacmcfg->DelNatIfaces(dev_name); // delete NAT-iface
				return;
			}
		}
		break;

	case IPA_CFG_CHANGE_EVENT:
		{
			if ( IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat != ipa_if_cate)
			{
				IPACMDBG_H("Received IPA_CFG_CHANGE_EVENT and category changed\n");
				/* delete previous instance */
				handle_down_evt();
				IPACM_Iface::ipacmcfg->DelNatIfaces(dev_name); // delete NAT-iface
				is_mode_switch = true; // need post internal usb-link up event
				return;
			}
			/* Add Natting iface to IPACM_Config if there is  Rx/Tx property */
			if (rx_prop != NULL || tx_prop != NULL)
			{
				IPACMDBG_H(" Has rx/tx properties registered for iface %s, add for NATTING \n", dev_name);
				IPACM_Iface::ipacmcfg->AddNatIfaces(dev_name);
			}
		}
		break;

	case IPA_PRIVATE_SUBNET_CHANGE_EVENT:
		{
			ipacm_event_data_fid *data = (ipacm_event_data_fid *)param;
			/* internel event: data->if_index is ipa_if_index */
			if (data->if_index == ipa_if_num)
			{
				IPACMDBG_H("Received IPA_PRIVATE_SUBNET_CHANGE_EVENT from itself posting, ignore\n");
				return;
			}
			else
			{
				IPACMDBG_H("Received IPA_PRIVATE_SUBNET_CHANGE_EVENT from other LAN iface \n");
#ifdef FEATURE_IPA_ANDROID
				handle_private_subnet_android(IPA_IP_v4);
#endif
				IPACMDBG_H(" delete old private subnet rules, use new sets \n");
				return;
			}
		}
		break;

	case IPA_LAN_DELETE_SELF:
	{
		ipacm_event_data_fid *data = (ipacm_event_data_fid *)param;
		if(data->if_index == ipa_if_num)
		{
			IPACMDBG_H("Received IPA_LAN_DELETE_SELF event.\n");
			IPACMDBG_H("ipa_LAN (%s):ipa_index (%d) instance close \n", IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].iface_name, ipa_if_num);
			/* posting link-up event for cradle use-case */
			if(is_mode_switch)
			{
				IPACMDBG_H("Posting IPA_USB_LINK_UP_EVENT event for (%s)\n", dev_name);
				ipacm_cmd_q_data evt_data;
				memset(&evt_data, 0, sizeof(evt_data));

				ipacm_event_data_fid *data_fid = NULL;
				data_fid = (ipacm_event_data_fid *)malloc(sizeof(ipacm_event_data_fid));
				if(data_fid == NULL)
				{
					IPACMERR("unable to allocate memory for IPA_USB_LINK_UP_EVENT data_fid\n");
					return;
				}
				if(IPACM_Iface::ipa_get_if_index(dev_name, &(data_fid->if_index)))
				{
					IPACMERR("Error while getting interface index for %s device", dev_name);
				}
				evt_data.event = IPA_USB_LINK_UP_EVENT;
				evt_data.evt_data = data_fid;
				//IPACMDBG_H("Posting event:%d\n", evt_data.event);
				IPACM_EvtDispatcher::PostEvt(&evt_data);
			}
#ifndef FEATURE_IPA_ANDROID
			if(rx_prop != NULL)
			{
				if(IPACM_Iface::ipacmcfg->getFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4) != 0)
				{
					IPACMDBG_DMESG("### WARNING ### num ipv4 flt rules on client %d is not expected: %d expected value: 0",
						rx_prop->rx[0].src_pipe, IPACM_Iface::ipacmcfg->getFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4));
				}
				if(IPACM_Iface::ipacmcfg->getFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6) != 0)
				{
					IPACMDBG_DMESG("### WARNING ### num ipv6 flt rules on client %d is not expected: %d expected value: 0",
						rx_prop->rx[0].src_pipe, IPACM_Iface::ipacmcfg->getFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6));
				}
			}
#endif
			delete this;
		}
		break;
	}

	case IPA_ADDR_ADD_EVENT:
		{
			ipacm_event_data_addr *data = (ipacm_event_data_addr *)param;
			ipa_interface_index = iface_ipa_index_query(data->if_index);

			if ( (data->iptype == IPA_IP_v4 && data->ipv4_addr == 0) ||
					 (data->iptype == IPA_IP_v6 &&
						data->ipv6_addr[0] == 0 && data->ipv6_addr[1] == 0 &&
					  data->ipv6_addr[2] == 0 && data->ipv6_addr[3] == 0) )
			{
				IPACMDBG_H("Invalid address, ignore IPA_ADDR_ADD_EVENT event\n");
				return;
			}


			if (ipa_interface_index == ipa_if_num)
			{
				IPACMDBG_H("Received IPA_ADDR_ADD_EVENT\n");

				/* only call ioctl for ODU iface with bridge mode */
				if((IPACM_Iface::ipacmcfg->ipacm_odu_enable == true) && (IPACM_Iface::ipacmcfg->ipacm_odu_router_mode == false)
						&& (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF))
				{
					if((data->iptype == IPA_IP_v6) && (num_dft_rt_v6 == 0))
					{
						handle_addr_evt_odu_bridge(data);
					}
#ifdef FEATURE_IPA_ANDROID
					add_dummy_private_subnet_flt_rule(data->iptype);
					handle_private_subnet_android(data->iptype);
#else
					handle_private_subnet(data->iptype);
#endif
				}
				else
				{

					/* check v4 not setup before, v6 can have 2 iface ip */
					if( ((data->iptype != ip_type) && (ip_type != IPA_IP_MAX))
						|| ((data->iptype==IPA_IP_v6) && (num_dft_rt_v6!=MAX_DEFAULT_v6_ROUTE_RULES)))
					{
						IPACMDBG_H("Got IPA_ADDR_ADD_EVENT ip-family:%d, v6 num %d: \n",data->iptype,num_dft_rt_v6);
						if(handle_addr_evt(data) == IPACM_FAILURE)
						{
							return;
						}
						/* ADD ipv4 icmp rule */
						if (data->iptype == IPA_IP_v4)
						{
							install_ipv4_icmp_flt_rule();
						}
						/* ADD ipv6 icmp rule */
						if ((num_dft_rt_v6 == 1) && (data->iptype == IPA_IP_v6))
						{
							install_ipv6_icmp_flt_rule();
						}

#ifdef FEATURE_IPA_ANDROID
						add_dummy_private_subnet_flt_rule(data->iptype);
						handle_private_subnet_android(data->iptype);
#else
						handle_private_subnet(data->iptype);
#endif

						if (IPACM_Wan::isWanUP(ipa_if_num))
						{
							if(data->iptype == IPA_IP_v4 || data->iptype == IPA_IP_MAX)
							{
								if(IPACM_Wan::backhaul_is_sta_mode == false)
								{
									ext_prop = IPACM_Iface::ipacmcfg->GetExtProp(IPA_IP_v4);
									handle_wan_up_ex(ext_prop, IPA_IP_v4,
												IPACM_Wan::getXlat_Mux_Id());
								}
								else
								{
									handle_wan_up(IPA_IP_v4);
								}
							}
						}

						if(IPACM_Wan::isWanUP_V6(ipa_if_num))
						{
							if((data->iptype == IPA_IP_v6 || data->iptype == IPA_IP_MAX) && num_dft_rt_v6 == 1)
							{
								install_ipv6_prefix_flt_rule(IPACM_Wan::backhaul_ipv6_prefix);
								if(IPACM_Wan::backhaul_is_sta_mode == false)
								{
									ext_prop = IPACM_Iface::ipacmcfg->GetExtProp(IPA_IP_v6);
									handle_wan_up_ex(ext_prop, IPA_IP_v6, 0);
								}
								else
								{
									handle_wan_up(IPA_IP_v6);
								}
							}
						}

						/* Post event to NAT */
						if (data->iptype == IPA_IP_v4)
						{
							ipacm_cmd_q_data evt_data;
							ipacm_event_iface_up *info;

							info = (ipacm_event_iface_up *)
								malloc(sizeof(ipacm_event_iface_up));
							if (info == NULL)
							{
								IPACMERR("Unable to allocate memory\n");
								return;
							}

							memcpy(info->ifname, dev_name, IF_NAME_LEN);
							info->ipv4_addr = data->ipv4_addr;
							info->addr_mask = IPACM_Iface::ipacmcfg->private_subnet_table[0].subnet_mask;

							evt_data.event = IPA_HANDLE_LAN_UP;
							evt_data.evt_data = (void *)info;

							/* Insert IPA_HANDLE_LAN_UP to command queue */
							IPACMDBG_H("posting IPA_HANDLE_LAN_UP for IPv4 with below information\n");
							IPACMDBG_H("IPv4 address:0x%x, IPv4 address mask:0x%x\n",
											info->ipv4_addr, info->addr_mask);
							IPACM_EvtDispatcher::PostEvt(&evt_data);
						}
						IPACMDBG_H("Finish handling IPA_ADDR_ADD_EVENT for ip-family(%d)\n", data->iptype);
					}

					IPACMDBG_H("Finish handling IPA_ADDR_ADD_EVENT for ip-family(%d)\n", data->iptype);
					/* checking if SW-RT_enable */
					if (IPACM_Iface::ipacmcfg->ipa_sw_rt_enable == true)
					{
						/* handle software routing enable event*/
						IPACMDBG_H("IPA_SW_ROUTING_ENABLE for iface: %s \n",IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].iface_name);
						handle_software_routing_enable();
					}

				}
			}
		}
		break;
#ifdef FEATURE_IPA_ANDROID
	case IPA_HANDLE_WAN_UP_TETHER:
		IPACMDBG_H("Received IPA_HANDLE_WAN_UP_TETHER event\n");

		data_wan_tether = (ipacm_event_iface_up_tehter*)param;
		if(data_wan_tether == NULL)
		{
			IPACMERR("No event data is found.\n");
			return;
		}
		IPACMDBG_H("Backhaul is sta mode?%d, if_index_tether:%d tether_if_name:%s\n", data_wan_tether->is_sta,
					data_wan_tether->if_index_tether,
					IPACM_Iface::ipacmcfg->iface_table[data_wan_tether->if_index_tether].iface_name);
		if (data_wan_tether->if_index_tether == ipa_if_num)
		{
			if(ip_type == IPA_IP_v4 || ip_type == IPA_IP_MAX)
			{
				if(data_wan_tether->is_sta == false)
				{
					ext_prop = IPACM_Iface::ipacmcfg->GetExtProp(IPA_IP_v4);
					handle_wan_up_ex(ext_prop, IPA_IP_v4, 0);
				}
				else
				{
					handle_wan_up(IPA_IP_v4);
				}
			}
		}
		break;

	case IPA_HANDLE_WAN_UP_V6_TETHER:
		IPACMDBG_H("Received IPA_HANDLE_WAN_UP_V6_TETHER event\n");

		data_wan_tether = (ipacm_event_iface_up_tehter*)param;
		if(data_wan_tether == NULL)
		{
			IPACMERR("No event data is found.\n");
			return;
		}
		IPACMDBG_H("Backhaul is sta mode?%d, if_index_tether:%d tether_if_name:%s\n", data_wan_tether->is_sta,
					data_wan_tether->if_index_tether,
					IPACM_Iface::ipacmcfg->iface_table[data_wan_tether->if_index_tether].iface_name);
		if (data_wan_tether->if_index_tether == ipa_if_num)
		{
			if(ip_type == IPA_IP_v6 || ip_type == IPA_IP_MAX)
			{
					install_ipv6_prefix_flt_rule(data_wan_tether->ipv6_prefix);
					if(data_wan_tether->is_sta == false)
					{
						ext_prop = IPACM_Iface::ipacmcfg->GetExtProp(IPA_IP_v6);
						handle_wan_up_ex(ext_prop, IPA_IP_v6, 0);
					}
					else
					{
						handle_wan_up(IPA_IP_v6);
					}
			}
		}
		break;

	case IPA_HANDLE_WAN_DOWN_TETHER:
		IPACMDBG_H("Received IPA_HANDLE_WAN_DOWN_TETHER event\n");
		data_wan_tether = (ipacm_event_iface_up_tehter*)param;
		if(data_wan_tether == NULL)
		{
			IPACMERR("No event data is found.\n");
			return;
		}
		IPACMDBG_H("Backhaul is sta mode?%d, if_index_tether:%d tether_if_name:%s\n", data_wan_tether->is_sta,
					data_wan_tether->if_index_tether,
					IPACM_Iface::ipacmcfg->iface_table[data_wan_tether->if_index_tether].iface_name);
		if (data_wan_tether->if_index_tether == ipa_if_num)
		{
			if(ip_type == IPA_IP_v4 || ip_type == IPA_IP_MAX)
			{
				handle_wan_down(data_wan_tether->is_sta);
			}
		}
		break;

	case IPA_HANDLE_WAN_DOWN_V6_TETHER:
		IPACMDBG_H("Received IPA_HANDLE_WAN_DOWN_V6_TETHER event\n");
		data_wan_tether = (ipacm_event_iface_up_tehter*)param;
		if(data_wan_tether == NULL)
		{
			IPACMERR("No event data is found.\n");
			return;
		}
		IPACMDBG_H("Backhaul is sta mode?%d, if_index_tether:%d tether_if_name:%s\n", data_wan_tether->is_sta,
					data_wan_tether->if_index_tether,
					IPACM_Iface::ipacmcfg->iface_table[data_wan_tether->if_index_tether].iface_name);
		if (data_wan_tether->if_index_tether == ipa_if_num)
		{
			/* clean up v6 RT rules*/
			IPACMDBG_H("Received IPA_HANDLE_WAN_DOWN_V6_TETHER in LAN-instance and need clean up client IPv6 address \n");
			/* reset usb-client ipv6 rt-rules */
			handle_lan_client_reset_rt(IPA_IP_v6);

			if(ip_type == IPA_IP_v6 || ip_type == IPA_IP_MAX)
			{
				handle_wan_down_v6(data_wan_tether->is_sta);
			}
		}
		break;
#else
	case IPA_HANDLE_WAN_UP:
		IPACMDBG_H("Received IPA_HANDLE_WAN_UP event\n");

		data_wan = (ipacm_event_iface_up*)param;
		if(data_wan == NULL)
		{
			IPACMERR("No event data is found.\n");
			return;
		}
		IPACMDBG_H("Backhaul is sta mode?%d\n", data_wan->is_sta);
		if(ip_type == IPA_IP_v4 || ip_type == IPA_IP_MAX)
		{
		if(data_wan->is_sta == false)
		{
				ext_prop = IPACM_Iface::ipacmcfg->GetExtProp(IPA_IP_v4);
				handle_wan_up_ex(ext_prop, IPA_IP_v4, data_wan->xlat_mux_id);
		}
		else
		{
			handle_wan_up(IPA_IP_v4);
		}
		}
		break;

	case IPA_HANDLE_WAN_UP_V6:
		IPACMDBG_H("Received IPA_HANDLE_WAN_UP_V6 event\n");

		data_wan = (ipacm_event_iface_up*)param;
		if(data_wan == NULL)
		{
			IPACMERR("No event data is found.\n");
			return;
		}
		IPACMDBG_H("Backhaul is sta mode?%d\n", data_wan->is_sta);
		if(ip_type == IPA_IP_v6 || ip_type == IPA_IP_MAX)
		{
			install_ipv6_prefix_flt_rule(data_wan->ipv6_prefix);
		if(data_wan->is_sta == false)
		{
				ext_prop = IPACM_Iface::ipacmcfg->GetExtProp(IPA_IP_v6);
				handle_wan_up_ex(ext_prop, IPA_IP_v6, 0);
			}
		else
		{
			handle_wan_up(IPA_IP_v6);
		}
		}
		break;

	case IPA_HANDLE_WAN_DOWN:
		IPACMDBG_H("Received IPA_HANDLE_WAN_DOWN event\n");
		data_wan = (ipacm_event_iface_up*)param;
		if(data_wan == NULL)
		{
			IPACMERR("No event data is found.\n");
			return;
		}
		IPACMDBG_H("Backhaul is sta mode?%d\n", data_wan->is_sta);
		if(ip_type == IPA_IP_v4 || ip_type == IPA_IP_MAX)
		{
			handle_wan_down(data_wan->is_sta);
		}
		break;

	case IPA_HANDLE_WAN_DOWN_V6:
		IPACMDBG_H("Received IPA_HANDLE_WAN_DOWN_V6 event\n");
		data_wan = (ipacm_event_iface_up*)param;
		if(data_wan == NULL)
		{
			IPACMERR("No event data is found.\n");
			return;
		}
		/* clean up v6 RT rules*/
		IPACMDBG_H("Received IPA_WAN_V6_DOWN in LAN-instance and need clean up client IPv6 address \n");
		/* reset usb-client ipv6 rt-rules */
		handle_lan_client_reset_rt(IPA_IP_v6);

		IPACMDBG_H("Backhaul is sta mode?%d\n", data_wan->is_sta);
		if(ip_type == IPA_IP_v6 || ip_type == IPA_IP_MAX)
		{
			handle_wan_down_v6(data_wan->is_sta);
		}
		break;
#endif

	case IPA_NEIGH_CLIENT_IP_ADDR_ADD_EVENT:
		{
			ipacm_event_data_all *data = (ipacm_event_data_all *)param;
			ipa_interface_index = iface_ipa_index_query(data->if_index);
			IPACMDBG_H("Recieved IPA_NEIGH_CLIENT_IP_ADDR_ADD_EVENT event \n");
			IPACMDBG_H("check iface %s category: %d\n",IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].iface_name, IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat);

			if ((ipa_interface_index == ipa_if_num) && (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF))
			{
				IPACMDBG_H("ODU iface got v4-ip \n");
				/* first construc ODU full header */
				if ((ipv4_header_set == false) && (ipv6_header_set == false))
				{
					/* construct ODU RT tbl */
					handle_odu_hdr_init(data->mac_addr);
					if (IPACM_Iface::ipacmcfg->ipacm_odu_embms_enable == true)
					{
						handle_odu_route_add();
						IPACMDBG_H("construct ODU header and route rules, embms_flag (%d) \n", IPACM_Iface::ipacmcfg->ipacm_odu_embms_enable);
					}
					else
					{
						IPACMDBG_H("construct ODU header only, embms_flag (%d) \n", IPACM_Iface::ipacmcfg->ipacm_odu_embms_enable);
					}
				}
				/* if ODU in bridge mode, directly return */
				if(IPACM_Iface::ipacmcfg->ipacm_odu_router_mode == false)
				{
					IPACMDBG_H("ODU is in bridge mode, no action \n");
					return;
				}
			}

			if (ipa_interface_index == ipa_if_num)
			{
				IPACMDBG_H("ETH iface got client \n");
				/* first construc ETH full header */
				handle_eth_hdr_init(data->mac_addr);
				handle_lan2lan_client_active(data, IPA_LAN_CLIENT_ACTIVE);
				IPACMDBG_H("construct ETH header and route rules \n");
				/* Associate with IP and construct RT-rule */
				if (handle_eth_client_ipaddr(data) == IPACM_FAILURE)
				{
					return;
				}
				handle_eth_client_route_rule(data->mac_addr, data->iptype);
				if (data->iptype == IPA_IP_v4)
				{
					/* Add NAT rules after ipv4 RT rules are set */
					CtList->HandleNeighIpAddrAddEvt(data);
				}
#ifdef FEATURE_ETH_BRIDGE_LE
				if (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
				{
					if (IPACM_Lan::cpe_to_usb_hdr_proc_ctx.valid == true)
					{
						eth_bridge_add_lan_client_rt_rule(data->mac_addr, SRC_LAN, IPA_IP_v4);
						eth_bridge_add_lan_client_rt_rule(data->mac_addr, SRC_LAN, IPA_IP_v6);
					}
					if (IPACM_Lan::wlan_to_usb_hdr_proc_ctx.valid == true)
					{
						eth_bridge_add_lan_client_rt_rule(data->mac_addr, SRC_WLAN, IPA_IP_v4);
						eth_bridge_add_lan_client_rt_rule(data->mac_addr, SRC_WLAN, IPA_IP_v6);
					}
				}
				if (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
				{
					if (IPACM_Lan::usb_to_cpe_hdr_proc_ctx.valid == true)
					{
						eth_bridge_add_lan_client_rt_rule(data->mac_addr, SRC_LAN, IPA_IP_v4);
						eth_bridge_add_lan_client_rt_rule(data->mac_addr, SRC_LAN, IPA_IP_v6);
					}
					if (IPACM_Lan::wlan_to_cpe_hdr_proc_ctx.valid == true)
					{
						eth_bridge_add_lan_client_rt_rule(data->mac_addr, SRC_WLAN, IPA_IP_v4);
						eth_bridge_add_lan_client_rt_rule(data->mac_addr, SRC_WLAN, IPA_IP_v6);
					}
				}
				eth_bridge_post_lan_client_event(data->mac_addr, IPA_ETH_BRIDGE_LAN_CLIENT_ADD_EVENT);
				eth_bridge_add_lan_client(data->mac_addr);
#endif
				return;
			}
		}
		break;

	case IPA_NEIGH_CLIENT_IP_ADDR_DEL_EVENT:
		{
			ipacm_event_data_all *data = (ipacm_event_data_all *)param;
			ipa_interface_index = iface_ipa_index_query(data->if_index);

			IPACMDBG_H("Received IPA_NEIGH_CLIENT_IP_ADDR_DEL_EVENT event. \n");
			IPACMDBG_H("check iface %s category: %d\n",IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].iface_name, IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat);
			/* if ODU in bridge mode, directly return */
			if ((ipa_interface_index == ipa_if_num) && (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
					&& (IPACM_Iface::ipacmcfg->ipacm_odu_router_mode == false))
			{
				IPACMDBG_H("ODU is in bridge mode, no action \n");
				return;
			}

			if (ipa_interface_index == ipa_if_num)
			{
				if (data->iptype == IPA_IP_v6)
				{
					handle_del_ipv6_addr(data);
					return;
				}
#ifdef FEATURE_ETH_BRIDGE_LE
				if (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
				{
					if (IPACM_Lan::cpe_to_usb_hdr_proc_ctx.valid == true)
					{
						eth_bridge_del_lan_client_rt_rule(data->mac_addr, SRC_LAN);
					}
					if (IPACM_Lan::wlan_to_usb_hdr_proc_ctx.valid == true)
					{
						eth_bridge_del_lan_client_rt_rule(data->mac_addr, SRC_WLAN);
					}
				}
				if (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
				{
					if (IPACM_Lan::usb_to_cpe_hdr_proc_ctx.valid == true)
					{
						eth_bridge_del_lan_client_rt_rule(data->mac_addr, SRC_LAN);
					}
					if (IPACM_Lan::wlan_to_cpe_hdr_proc_ctx.valid == true)
					{
						eth_bridge_del_lan_client_rt_rule(data->mac_addr, SRC_WLAN);
					}
				}
				eth_bridge_post_lan_client_event(data->mac_addr, IPA_ETH_BRIDGE_LAN_CLIENT_DEL_EVENT);
				eth_bridge_del_lan_client(data->mac_addr);
#endif
				IPACMDBG_H("LAN iface delete client \n");
				handle_eth_client_down_evt(data->mac_addr);
				handle_lan2lan_client_active(data, IPA_LAN_CLIENT_INACTIVE);
				return;
			}
		}
		break;

	case IPA_SW_ROUTING_ENABLE:
		IPACMDBG_H("Received IPA_SW_ROUTING_ENABLE\n");
		/* handle software routing enable event*/
		handle_software_routing_enable();
		break;

	case IPA_SW_ROUTING_DISABLE:
		IPACMDBG_H("Received IPA_SW_ROUTING_DISABLE\n");
		/* handle software routing disable event*/
		handle_software_routing_disable();
		break;

	case IPA_ETH_BRIDGE_LAN_CLIENT_ADD_EVENT:
			{
				IPACMDBG_H("Received IPA_ETH_BRIDGE_LAN_CLIENT_ADD_EVENT event.\n");
				ipacm_event_data_mac* mac = (ipacm_event_data_mac*)param;
				if(mac != NULL)
				{
					if (mac->if_index == ipa_if_num)
					{
						IPACMDBG_H("The event was sent by same interface, if_index: %d ignore. \n", mac->if_index);
						return;
					}
					if(ip_type == IPA_IP_v4 || ip_type == IPA_IP_MAX)
					{
						eth_bridge_add_lan_client_flt_rule(mac->mac_addr, IPA_IP_v4);
					}
					if(ip_type == IPA_IP_v6 || ip_type == IPA_IP_MAX)
					{
						eth_bridge_add_lan_client_flt_rule(mac->mac_addr, IPA_IP_v6);
					}
				}
				else
				{
					IPACMERR("Event MAC is empty.\n");
				}
			}
			break;

	case IPA_ETH_BRIDGE_LAN_CLIENT_DEL_EVENT:
			{
				IPACMDBG_H("Received IPA_ETH_BRIDGE_LAN_CLIENT_DEL_EVENT event.\n");
				ipacm_event_data_mac* mac = (ipacm_event_data_mac*)param;
				if(mac != NULL)
				{
					if (mac->if_index == ipa_if_num)
					{
						IPACMDBG_H("The event was sent by same interface, if_index: %d ignore. \n", mac->if_index);
						return;
					}
					if(eth_bridge_del_lan_client_flt_rule(mac->mac_addr) == IPACM_FAILURE)
					{
						IPACMDBG_H("Failed to delete lan client MAC based flt rule.\n");
					}
				}
				else
				{
					IPACMERR("Event MAC is empty.\n");
				}
			}
			break;

	case IPA_ETH_BRIDGE_WLAN_CLIENT_ADD_EVENT:
		{
			IPACMDBG_H("Received IPA_ETH_BRIDGE_WLAN_CLIENT_ADD_EVENT event.\n");
			ipacm_event_data_mac* mac = (ipacm_event_data_mac*)param;
			if(mac != NULL)
			{
				if(ip_type == IPA_IP_v4 || ip_type == IPA_IP_MAX)
				{
					eth_bridge_add_wlan_client_flt_rule(mac->mac_addr, IPA_IP_v4);
				}
				if(ip_type == IPA_IP_v6 || ip_type == IPA_IP_MAX)
				{
					eth_bridge_add_wlan_client_flt_rule(mac->mac_addr, IPA_IP_v6);
				}
			}
			else
			{
				IPACMERR("Event MAC is empty.\n");
			}
		}
		break;

	case IPA_ETH_BRIDGE_WLAN_CLIENT_DEL_EVENT:
		{
			IPACMDBG_H("Received IPA_ETH_BRIDGE_WLAN_CLIENT_DEL_EVENT event.\n");
			ipacm_event_data_mac* mac = (ipacm_event_data_mac*)param;
			if(mac != NULL)
			{
				if(eth_bridge_del_wlan_client_flt_rule(mac->mac_addr) == IPACM_FAILURE)
				{
					IPACMDBG_H("Failed to delete wlan client MAC based flt rule.\n");
				}
			}
			else
			{
				IPACMERR("Event MAC is empty.\n");
			}
		}
		break;

	case IPA_ETH_BRIDGE_HDR_PROC_CTX_SET_EVENT:
	{
		IPACMDBG_H("Received IPA_ETH_BRIDGE_HDR_PROC_CTX_SET_EVENT event.\n");
		int i;
		ipacm_event_data_if_cat* cat = (ipacm_event_data_if_cat*)param;
		if(cat == NULL)
		{
			IPACMERR("Event data is empty.\n");
			return;
		}
		if (cat->if_cat == IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat)
		{
			IPACMDBG_H("The event was sent by same interface, if_cat: %d ignore. \n", cat->if_cat);
			return;
		}

		for(i=0; i<IPACM_Lan::num_lan_client; i++)
		{
			if(IPACM_Lan::eth_bridge_lan_client[i].ipa_if_num == ipa_if_num)
			{
				if (cat->if_cat == WLAN_IF)
				{
					eth_bridge_add_lan_client_rt_rule(IPACM_Lan::eth_bridge_lan_client[i].mac, SRC_WLAN, IPA_IP_v4);
					eth_bridge_add_lan_client_rt_rule(IPACM_Lan::eth_bridge_lan_client[i].mac, SRC_WLAN, IPA_IP_v6);
				}
				else
				{
					eth_bridge_add_lan_client_rt_rule(IPACM_Lan::eth_bridge_lan_client[i].mac, SRC_LAN, IPA_IP_v4);
					eth_bridge_add_lan_client_rt_rule(IPACM_Lan::eth_bridge_lan_client[i].mac, SRC_LAN, IPA_IP_v6);
				}
			}
		}
	}
	break;

	case IPA_ETH_BRIDGE_HDR_PROC_CTX_UNSET_EVENT:
	{
		IPACMDBG_H("Received IPA_ETH_BRIDGE_HDR_PROC_CTX_UNSET_EVENT event.\n");
		int i;
		ipacm_event_data_if_cat* cat = (ipacm_event_data_if_cat*)param;
		if(cat == NULL)
		{
			IPACMERR("Event data is empty.\n");
			return;
		}
		if (cat->if_cat == IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat)
		{
			IPACMDBG_H("The event was sent by same interface, if_cat: %d ignore.\n", cat->if_cat);
			return;
		}

		for(i=0; i<IPACM_Lan::num_lan_client; i++)
		{
			if(IPACM_Lan::eth_bridge_lan_client[i].ipa_if_num == ipa_if_num)
			{
				if (cat->if_cat == WLAN_IF)
				{
					eth_bridge_del_lan_client_rt_rule(IPACM_Lan::eth_bridge_lan_client[i].mac, SRC_WLAN);
				}
				else
				{
					eth_bridge_del_lan_client_rt_rule(IPACM_Lan::eth_bridge_lan_client[i].mac, SRC_LAN);
				}
			}
		}
	}
	break;

	case IPA_CRADLE_WAN_MODE_SWITCH:
	{
		IPACMDBG_H("Received IPA_CRADLE_WAN_MODE_SWITCH event.\n");
		ipacm_event_cradle_wan_mode* wan_mode = (ipacm_event_cradle_wan_mode*)param;
		if(wan_mode == NULL)
		{
			IPACMERR("Event data is empty.\n");
			return;
		}

		if(wan_mode->cradle_wan_mode == BRIDGE)
		{
			handle_cradle_wan_mode_switch(true);
		}
		else
		{
			handle_cradle_wan_mode_switch(false);
		}
	}
	break;

	case IPA_TETHERING_STATS_UPDATE_EVENT:
	{
		IPACMDBG_H("Received IPA_TETHERING_STATS_UPDATE_EVENT event.\n");
		if (IPACM_Wan::isWanUP(ipa_if_num) || IPACM_Wan::isWanUP_V6(ipa_if_num))
		{
			if(IPACM_Wan::backhaul_is_sta_mode == false) /* LTE */
			{
				ipa_get_data_stats_resp_msg_v01 *data = (ipa_get_data_stats_resp_msg_v01 *)param;
				IPACMDBG("Received IPA_TETHERING_STATS_UPDATE_STATS ipa_stats_type: %d\n",data->ipa_stats_type);
				IPACMDBG("Received %d UL, %d DL pipe stats\n",data->ul_src_pipe_stats_list_len,
					data->dl_dst_pipe_stats_list_len);
				if (data->ipa_stats_type != QMI_IPA_STATS_TYPE_PIPE_V01)
				{
					IPACMERR("not valid pipe stats enum(%d)\n", data->ipa_stats_type);
					return;
				}
				handle_tethering_stats_event(data);
			}
		}
	}
	break;

	default:
		break;
	}

	return;
}


int IPACM_Lan::handle_del_ipv6_addr(ipacm_event_data_all *data)
{
	uint32_t tx_index;
	uint32_t rt_hdl;
	int num_v6, clnt_indx;

	clnt_indx = get_eth_client_index(data->mac_addr);
	if (clnt_indx == IPACM_INVALID_INDEX)
	{
		IPACMERR("eth client not found/attached \n");
		return IPACM_FAILURE;
	}

	if(data->iptype == IPA_IP_v6)
	{
		if ((data->ipv6_addr[0] != 0) || (data->ipv6_addr[1] != 0) ||
				(data->ipv6_addr[2] != 0) || (data->ipv6_addr[3] || 0))
		{
			IPACMDBG_H("ipv6 address got: 0x%x:%x:%x:%x\n", data->ipv6_addr[0], data->ipv6_addr[1], data->ipv6_addr[2], data->ipv6_addr[3]);
			for(num_v6=0;num_v6 < get_client_memptr(eth_client, clnt_indx)->ipv6_set;num_v6++)
			{
				if( data->ipv6_addr[0] == get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6][0] &&
					data->ipv6_addr[1] == get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6][1] &&
					data->ipv6_addr[2]== get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6][2] &&
					data->ipv6_addr[3] == get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6][3])
				{
					IPACMDBG_H("ipv6 addr is found at position:%d for client:%d\n", num_v6, clnt_indx);
					break;
				}
			}
		}
		if (num_v6 == IPV6_NUM_ADDR)
		{
			IPACMDBG_H("ipv6 addr is not found. \n");
			return IPACM_FAILURE;
		}

		for(tx_index = 0; tx_index < iface_query->num_tx_props; tx_index++)
		{
			if((tx_prop->tx[tx_index].ip == IPA_IP_v6) && (get_client_memptr(eth_client, clnt_indx)->route_rule_set_v6 != 0))
			{
				IPACMDBG_H("Delete client index %d ipv6 RT-rules for %d-st ipv6 for tx:%d\n", clnt_indx, num_v6, tx_index);
				rt_hdl = get_client_memptr(eth_client, clnt_indx)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6[num_v6];
				if(m_routing.DeleteRoutingHdl(rt_hdl, IPA_IP_v6) == false)
				{
					return IPACM_FAILURE;
				}
				rt_hdl = get_client_memptr(eth_client, clnt_indx)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6_wan[num_v6];
				if(m_routing.DeleteRoutingHdl(rt_hdl, IPA_IP_v6) == false)
				{
					return IPACM_FAILURE;
				}
				get_client_memptr(eth_client, clnt_indx)->ipv6_set--;
				get_client_memptr(eth_client, clnt_indx)->route_rule_set_v6--;

				for(num_v6;num_v6< get_client_memptr(eth_client, clnt_indx)->ipv6_set;num_v6++)
				{
					get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6][0] =
						get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6+1][0];
					get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6][1] =
						get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6+1][1];
					get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6][2] =
						get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6+1][2];
					get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6][3] =
						get_client_memptr(eth_client, clnt_indx)->v6_addr[num_v6+1][3];
					get_client_memptr(eth_client, clnt_indx)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6[num_v6] =
						get_client_memptr(eth_client, clnt_indx)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6[num_v6+1];
					get_client_memptr(eth_client, clnt_indx)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6_wan[num_v6] =
						get_client_memptr(eth_client, clnt_indx)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6_wan[num_v6+1];
				}
			}
		}
	}
	return IPACM_SUCCESS;
}

/* delete filter rule for wan_down event for IPv4*/
int IPACM_Lan::handle_wan_down(bool is_sta_mode)
{
	ipa_fltr_installed_notif_req_msg_v01 flt_index;
	int fd;

	fd = open(IPA_DEVICE_NAME, O_RDWR);
	if (0 == fd)
	{
		IPACMERR("Failed opening %s.\n", IPA_DEVICE_NAME);
		return IPACM_FAILURE;
	}

	if(is_sta_mode == false)
	{
		if (num_wan_ul_fl_rule_v4 > MAX_WAN_UL_FILTER_RULES)
		{
			IPACMERR("number of wan_ul_fl_rule_v4 > MAX_WAN_UL_FILTER_RULES, aborting...\n");
			close(fd);
			return IPACM_FAILURE;
		}
		if (m_filtering.DeleteFilteringHdls(wan_ul_fl_rule_hdl_v4,
			IPA_IP_v4, num_wan_ul_fl_rule_v4) == false)
		{
			IPACMERR("Error Deleting RuleTable(1) to Filtering, aborting...\n");
			close(fd);
			return IPACM_FAILURE;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, num_wan_ul_fl_rule_v4);

		memset(wan_ul_fl_rule_hdl_v4, 0, MAX_WAN_UL_FILTER_RULES * sizeof(uint32_t));
		num_wan_ul_fl_rule_v4 = 0;

		memset(&flt_index, 0, sizeof(flt_index));
		flt_index.source_pipe_index = ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, rx_prop->rx[0].src_pipe);
		flt_index.install_status = IPA_QMI_RESULT_SUCCESS_V01;
#ifndef FEATURE_IPA_V3
		flt_index.filter_index_list_len = 0;
#else /* defined (FEATURE_IPA_V3) */
		flt_index.rule_id_valid = 1;
		flt_index.rule_id_len = 0;
#endif
		flt_index.embedded_pipe_index_valid = 1;
		flt_index.embedded_pipe_index = ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, IPA_CLIENT_APPS_LAN_WAN_PROD);
		flt_index.retain_header_valid = 1;
		flt_index.retain_header = 0;
		flt_index.embedded_call_mux_id_valid = 1;
		flt_index.embedded_call_mux_id = IPACM_Iface::ipacmcfg->GetQmapId();

		if(false == m_filtering.SendFilteringRuleIndex(&flt_index))
		{
			IPACMERR("Error sending filtering rule index, aborting...\n");
			close(fd);
			return IPACM_FAILURE;
		}
	}
	else
	{
		if (m_filtering.DeleteFilteringHdls(&lan_wan_fl_rule_hdl[0], IPA_IP_v4, 1) == false)
		{
			IPACMERR("Error Adding RuleTable(1) to Filtering, aborting...\n");
			close(fd);
			return IPACM_FAILURE;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, 1);
	}

	close(fd);
	return IPACM_SUCCESS;
}

/* handle new_address event*/
int IPACM_Lan::handle_addr_evt(ipacm_event_data_addr *data)
{
	struct ipa_ioc_add_rt_rule *rt_rule;
	struct ipa_rt_rule_add *rt_rule_entry;
	const int NUM_RULES = 1;
	int num_ipv6_addr;
	int res = IPACM_SUCCESS;

	IPACMDBG_H("set route/filter rule ip-type: %d \n", data->iptype);

/* Add private subnet*/
#ifdef FEATURE_IPA_ANDROID
if (data->iptype == IPA_IP_v4)
{
	IPACMDBG_H("current IPACM private subnet_addr number(%d)\n", IPACM_Iface::ipacmcfg->ipa_num_private_subnet);
	if_ipv4_subnet = (data->ipv4_addr >> 8) << 8;
	IPACMDBG_H(" Add IPACM private subnet_addr as: 0x%x \n", if_ipv4_subnet);
	if(IPACM_Iface::ipacmcfg->AddPrivateSubnet(if_ipv4_subnet, ipa_if_num) == false)
	{
		IPACMERR(" can't Add IPACM private subnet_addr as: 0x%x \n", if_ipv4_subnet);
	}
}
#endif /* defined(FEATURE_IPA_ANDROID)*/

	if (data->iptype == IPA_IP_v4)
	{
		rt_rule = (struct ipa_ioc_add_rt_rule *)
			 calloc(1, sizeof(struct ipa_ioc_add_rt_rule) +
							NUM_RULES * sizeof(struct ipa_rt_rule_add));

		if (!rt_rule)
		{
			IPACMERR("Error Locate ipa_ioc_add_rt_rule memory...\n");
			return IPACM_FAILURE;
		}

		rt_rule->commit = 1;
		rt_rule->num_rules = NUM_RULES;
		rt_rule->ip = data->iptype;
		rt_rule_entry = &rt_rule->rules[0];
		rt_rule_entry->at_rear = false;
		rt_rule_entry->rule.dst = IPA_CLIENT_APPS_LAN_CONS;  //go to A5
		rt_rule_entry->rule.attrib.attrib_mask = IPA_FLT_DST_ADDR;
   		strcpy(rt_rule->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_lan_v4.name);
		rt_rule_entry->rule.attrib.u.v4.dst_addr      = data->ipv4_addr;
		rt_rule_entry->rule.attrib.u.v4.dst_addr_mask = 0xFFFFFFFF;
		if (false == m_routing.AddRoutingRule(rt_rule))
		{
			IPACMERR("Routing rule addition failed!\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else if (rt_rule_entry->status)
		{
			IPACMERR("rt rule adding failed. Result=%d\n", rt_rule_entry->status);
			res = rt_rule_entry->status;
			goto fail;
		}
		dft_rt_rule_hdl[0] = rt_rule_entry->rt_rule_hdl;
		IPACMDBG_H("ipv4 iface rt-rule hdl1=0x%x\n", dft_rt_rule_hdl[0]);
		/* initial multicast/broadcast/fragment filter rule */
#ifdef FEATURE_ETH_BRIDGE_LE
		init_fl_rule(data->iptype);
		eth_bridge_handle_dummy_wlan_client_flt_rule(data->iptype);
		eth_bridge_handle_dummy_lan_client_flt_rule(data->iptype);
		eth_bridge_install_cache_wlan_client_flt_rule(data->iptype);
		eth_bridge_install_cache_lan_client_flt_rule(data->iptype);
#else
#ifdef CT_OPT
		install_tcp_ctl_flt_rule(IPA_IP_v4);
#endif
		init_fl_rule(data->iptype);
		add_dummy_lan2lan_flt_rule(data->iptype);
#endif
	}
	else
	{
	    /* check if see that v6-addr already or not*/
	    for(num_ipv6_addr=0;num_ipv6_addr<num_dft_rt_v6;num_ipv6_addr++)
	    {
            if((ipv6_addr[num_ipv6_addr][0] == data->ipv6_addr[0]) &&
	           (ipv6_addr[num_ipv6_addr][1] == data->ipv6_addr[1]) &&
	           (ipv6_addr[num_ipv6_addr][2] == data->ipv6_addr[2]) &&
	           (ipv6_addr[num_ipv6_addr][3] == data->ipv6_addr[3]))
            {
				return IPACM_FAILURE;
				break;
	        }
	    }

		rt_rule = (struct ipa_ioc_add_rt_rule *)
			 calloc(1, sizeof(struct ipa_ioc_add_rt_rule) +
							NUM_RULES * sizeof(struct ipa_rt_rule_add));

		if (!rt_rule)
		{
			IPACMERR("Error Locate ipa_ioc_add_rt_rule memory...\n");
			return IPACM_FAILURE;
		}

		rt_rule->commit = 1;
		rt_rule->num_rules = NUM_RULES;
		rt_rule->ip = data->iptype;
		strcpy(rt_rule->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_v6.name);

		rt_rule_entry = &rt_rule->rules[0];
		rt_rule_entry->at_rear = false;
		rt_rule_entry->rule.dst = IPA_CLIENT_APPS_LAN_CONS;  //go to A5
		rt_rule_entry->rule.attrib.attrib_mask = IPA_FLT_DST_ADDR;
		rt_rule_entry->rule.attrib.u.v6.dst_addr[0] = data->ipv6_addr[0];
		rt_rule_entry->rule.attrib.u.v6.dst_addr[1] = data->ipv6_addr[1];
		rt_rule_entry->rule.attrib.u.v6.dst_addr[2] = data->ipv6_addr[2];
		rt_rule_entry->rule.attrib.u.v6.dst_addr[3] = data->ipv6_addr[3];
		rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[0] = 0xFFFFFFFF;
		rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[1] = 0xFFFFFFFF;
		rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[2] = 0xFFFFFFFF;
		rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[3] = 0xFFFFFFFF;
		ipv6_addr[num_dft_rt_v6][0] = data->ipv6_addr[0];
		ipv6_addr[num_dft_rt_v6][1] = data->ipv6_addr[1];
		ipv6_addr[num_dft_rt_v6][2] = data->ipv6_addr[2];
		ipv6_addr[num_dft_rt_v6][3] = data->ipv6_addr[3];
		if (false == m_routing.AddRoutingRule(rt_rule))
		{
			IPACMERR("Routing rule addition failed!\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else if (rt_rule_entry->status)
		{
			IPACMERR("rt rule adding failed. Result=%d\n", rt_rule_entry->status);
			res = rt_rule_entry->status;
			goto fail;
		}
		dft_rt_rule_hdl[MAX_DEFAULT_v4_ROUTE_RULES + 2*num_dft_rt_v6] = rt_rule_entry->rt_rule_hdl;

		/* setup same rule for v6_wan table*/
		strcpy(rt_rule->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_wan_v6.name);
		if (false == m_routing.AddRoutingRule(rt_rule))
		{
			IPACMERR("Routing rule addition failed!\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else if (rt_rule_entry->status)
		{
			IPACMERR("rt rule adding failed. Result=%d\n", rt_rule_entry->status);
			res = rt_rule_entry->status;
			goto fail;
		}
		dft_rt_rule_hdl[MAX_DEFAULT_v4_ROUTE_RULES + 2*num_dft_rt_v6+1] = rt_rule_entry->rt_rule_hdl;

		IPACMDBG_H("ipv6 wan iface rt-rule hdl=0x%x hdl=0x%x, num_dft_rt_v6: %d \n",
		          dft_rt_rule_hdl[MAX_DEFAULT_v4_ROUTE_RULES + 2*num_dft_rt_v6],
		          dft_rt_rule_hdl[MAX_DEFAULT_v4_ROUTE_RULES + 2*num_dft_rt_v6+1],num_dft_rt_v6);

		if (num_dft_rt_v6 == 0)
		{
			/* initial multicast/broadcast/fragment filter rule */
#ifdef FEATURE_ETH_BRIDGE_LE
			eth_bridge_handle_dummy_wlan_client_flt_rule(data->iptype);
			eth_bridge_handle_dummy_lan_client_flt_rule(data->iptype);
			eth_bridge_install_cache_wlan_client_flt_rule(data->iptype);
			eth_bridge_install_cache_lan_client_flt_rule(data->iptype);
			init_fl_rule(data->iptype);
#else
#ifdef CT_OPT
			install_tcp_ctl_flt_rule(IPA_IP_v6);
#endif
			add_dummy_lan2lan_flt_rule(data->iptype);
			init_fl_rule(data->iptype);
#endif
		}
		num_dft_rt_v6++;
		IPACMDBG_H("number of default route rules %d\n", num_dft_rt_v6);
	}

	IPACMDBG_H("finish route/filter rule ip-type: %d, res(%d)\n", data->iptype, res);

fail:
	free(rt_rule);
	return res;
}

/* configure private subnet filter rules*/
int IPACM_Lan::handle_private_subnet(ipa_ip_type iptype)
{
	struct ipa_flt_rule_add flt_rule_entry;
	int i;

	ipa_ioc_add_flt_rule *m_pFilteringTable;

	IPACMDBG_H("lan->handle_private_subnet(); set route/filter rule \n");

	if (rx_prop == NULL)
	{
		IPACMDBG_H("No rx properties registered for iface %s\n", dev_name);
		return IPACM_SUCCESS;
	}

	if (iptype == IPA_IP_v4)
	{

		m_pFilteringTable = (struct ipa_ioc_add_flt_rule *)
			 calloc(1,
							sizeof(struct ipa_ioc_add_flt_rule) +
							(IPACM_Iface::ipacmcfg->ipa_num_private_subnet) * sizeof(struct ipa_flt_rule_add)
							);
		if (!m_pFilteringTable)
		{
			PERROR("Error Locate ipa_flt_rule_add memory...\n");
			return IPACM_FAILURE;
		}
		m_pFilteringTable->commit = 1;
		m_pFilteringTable->ep = rx_prop->rx[0].src_pipe;
		m_pFilteringTable->global = false;
		m_pFilteringTable->ip = IPA_IP_v4;
		m_pFilteringTable->num_rules = (uint8_t)IPACM_Iface::ipacmcfg->ipa_num_private_subnet;

		if (false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_lan_v4))
		{
			IPACMERR("LAN m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_lan_v4=0x%p) Failed.\n", &IPACM_Iface::ipacmcfg->rt_tbl_lan_v4);
			free(m_pFilteringTable);
			return IPACM_FAILURE;
		}

		/* Make LAN-traffic always go A5, use default IPA-RT table */
		if (false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_default_v4))
		{
			IPACMERR("LAN m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_default_v4=0x%p) Failed.\n", &IPACM_Iface::ipacmcfg->rt_tbl_default_v4);
			free(m_pFilteringTable);
			return IPACM_FAILURE;
		}

		for (i = 0; i < (IPACM_Iface::ipacmcfg->ipa_num_private_subnet); i++)
		{
			memset(&flt_rule_entry, 0, sizeof(struct ipa_flt_rule_add));
			flt_rule_entry.at_rear = true;
			flt_rule_entry.rule.retain_hdr = 1;
			flt_rule_entry.flt_rule_hdl = -1;
			flt_rule_entry.status = -1;
			flt_rule_entry.rule.action = IPA_PASS_TO_ROUTING;
                        /* Support private subnet feature including guest-AP can't talk to primary AP etc */
			flt_rule_entry.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_default_v4.hdl;
			IPACMDBG_H(" private filter rule use table: %s\n",IPACM_Iface::ipacmcfg->rt_tbl_default_v4.name);

			memcpy(&flt_rule_entry.rule.attrib,
						 &rx_prop->rx[0].attrib,
						 sizeof(flt_rule_entry.rule.attrib));
			flt_rule_entry.rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
			flt_rule_entry.rule.attrib.u.v4.dst_addr_mask = IPACM_Iface::ipacmcfg->private_subnet_table[i].subnet_mask;
			flt_rule_entry.rule.attrib.u.v4.dst_addr = IPACM_Iface::ipacmcfg->private_subnet_table[i].subnet_addr;
			memcpy(&(m_pFilteringTable->rules[i]), &flt_rule_entry, sizeof(struct ipa_flt_rule_add));
			IPACMDBG_H("Loop %d  5\n", i);
		}

		if (false == m_filtering.AddFilteringRule(m_pFilteringTable))
		{
			IPACMERR("Error Adding RuleTable(0) to Filtering, aborting...\n");
			free(m_pFilteringTable);
			return IPACM_FAILURE;
		}
		IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, IPACM_Iface::ipacmcfg->ipa_num_private_subnet);

		/* copy filter rule hdls */
		for (i = 0; i < IPACM_Iface::ipacmcfg->ipa_num_private_subnet; i++)
		{
			private_fl_rule_hdl[i] = m_pFilteringTable->rules[i].flt_rule_hdl;
		}
		free(m_pFilteringTable);
	}
	else
	{
		IPACMDBG_H("No private subnet rules for ipv6 iface %s\n", dev_name);
	}
	return IPACM_SUCCESS;
}


/* for STA mode wan up:  configure filter rule for wan_up event*/
int IPACM_Lan::handle_wan_up(ipa_ip_type ip_type)
{
	struct ipa_flt_rule_add flt_rule_entry;
	int len = 0;
	ipa_ioc_add_flt_rule *m_pFilteringTable;

	IPACMDBG_H("set WAN interface as default filter rule\n");

	if (rx_prop == NULL)
	{
		IPACMDBG_H("No rx properties registered for iface %s\n", dev_name);
		return IPACM_SUCCESS;
	}

	if(ip_type == IPA_IP_v4)
	{
		len = sizeof(struct ipa_ioc_add_flt_rule) + (1 * sizeof(struct ipa_flt_rule_add));
		m_pFilteringTable = (struct ipa_ioc_add_flt_rule *)calloc(1, len);
		if (m_pFilteringTable == NULL)
		{
			PERROR("Error Locate ipa_flt_rule_add memory...\n");
			return IPACM_FAILURE;
		}

		m_pFilteringTable->commit = 1;
		m_pFilteringTable->ep = rx_prop->rx[0].src_pipe;
		m_pFilteringTable->global = false;
		m_pFilteringTable->ip = IPA_IP_v4;
		m_pFilteringTable->num_rules = (uint8_t)1;

		IPACMDBG_H("Retrieving routing hanle for table: %s\n",
						 IPACM_Iface::ipacmcfg->rt_tbl_wan_v4.name);
		if (false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_wan_v4))
		{
			IPACMERR("m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_wan_v4=0x%p) Failed.\n",
							 &IPACM_Iface::ipacmcfg->rt_tbl_wan_v4);
			free(m_pFilteringTable);
			return IPACM_FAILURE;
		}
		IPACMDBG_H("Routing hanle for table: %d\n", IPACM_Iface::ipacmcfg->rt_tbl_wan_v4.hdl);


		memset(&flt_rule_entry, 0, sizeof(struct ipa_flt_rule_add)); // Zero All Fields
		flt_rule_entry.at_rear = true;
		flt_rule_entry.flt_rule_hdl = -1;
		flt_rule_entry.status = -1;
		if(IPACM_Wan::isWan_Bridge_Mode())
		{
			flt_rule_entry.rule.action = IPA_PASS_TO_ROUTING;
		}
		else
		{
			flt_rule_entry.rule.action = IPA_PASS_TO_SRC_NAT; //IPA_PASS_TO_ROUTING
		}
		flt_rule_entry.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_wan_v4.hdl;

		memcpy(&flt_rule_entry.rule.attrib,
					 &rx_prop->rx[0].attrib,
					 sizeof(flt_rule_entry.rule.attrib));

		flt_rule_entry.rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
		flt_rule_entry.rule.attrib.u.v4.dst_addr_mask = 0x0;
		flt_rule_entry.rule.attrib.u.v4.dst_addr = 0x0;

		memcpy(&m_pFilteringTable->rules[0], &flt_rule_entry, sizeof(flt_rule_entry));
		if (false == m_filtering.AddFilteringRule(m_pFilteringTable))
		{
			IPACMERR("Error Adding RuleTable(0) to Filtering, aborting...\n");
			free(m_pFilteringTable);
			return IPACM_FAILURE;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, 1);
			IPACMDBG_H("flt rule hdl0=0x%x, status=0x%x\n",
							 m_pFilteringTable->rules[0].flt_rule_hdl,
							 m_pFilteringTable->rules[0].status);
		}


		/* copy filter hdls  */
		lan_wan_fl_rule_hdl[0] = m_pFilteringTable->rules[0].flt_rule_hdl;
		free(m_pFilteringTable);
	}
	else if(ip_type == IPA_IP_v6)
	{
		/* add default v6 filter rule */
		m_pFilteringTable = (struct ipa_ioc_add_flt_rule *)
			 calloc(1, sizeof(struct ipa_ioc_add_flt_rule) +
					1 * sizeof(struct ipa_flt_rule_add));

		if (!m_pFilteringTable)
		{
			PERROR("Error Locate ipa_flt_rule_add memory...\n");
			return IPACM_FAILURE;
		}

		m_pFilteringTable->commit = 1;
		m_pFilteringTable->ep = rx_prop->rx[0].src_pipe;
		m_pFilteringTable->global = false;
		m_pFilteringTable->ip = IPA_IP_v6;
		m_pFilteringTable->num_rules = (uint8_t)1;

		if (false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_v6))
		{
			IPACMERR("m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_v6=0x%p) Failed.\n", &IPACM_Iface::ipacmcfg->rt_tbl_v6);
			free(m_pFilteringTable);
			return IPACM_FAILURE;
		}

		memset(&flt_rule_entry, 0, sizeof(struct ipa_flt_rule_add));

		flt_rule_entry.at_rear = true;
		flt_rule_entry.flt_rule_hdl = -1;
		flt_rule_entry.status = -1;
		flt_rule_entry.rule.action = IPA_PASS_TO_ROUTING;
		flt_rule_entry.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_v6.hdl;

		memcpy(&flt_rule_entry.rule.attrib,
					 &rx_prop->rx[0].attrib,
					 sizeof(flt_rule_entry.rule.attrib));

		flt_rule_entry.rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
		flt_rule_entry.rule.attrib.u.v6.dst_addr_mask[0] = 0x00000000;
		flt_rule_entry.rule.attrib.u.v6.dst_addr_mask[1] = 0x00000000;
		flt_rule_entry.rule.attrib.u.v6.dst_addr_mask[2] = 0x00000000;
		flt_rule_entry.rule.attrib.u.v6.dst_addr_mask[3] = 0x00000000;
		flt_rule_entry.rule.attrib.u.v6.dst_addr[0] = 0X00000000;
		flt_rule_entry.rule.attrib.u.v6.dst_addr[1] = 0x00000000;
		flt_rule_entry.rule.attrib.u.v6.dst_addr[2] = 0x00000000;
		flt_rule_entry.rule.attrib.u.v6.dst_addr[3] = 0X00000000;

		memcpy(&(m_pFilteringTable->rules[0]), &flt_rule_entry, sizeof(struct ipa_flt_rule_add));
		if (false == m_filtering.AddFilteringRule(m_pFilteringTable))
		{
			IPACMERR("Error Adding Filtering rule, aborting...\n");
			free(m_pFilteringTable);
			return IPACM_FAILURE;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, 1);
			IPACMDBG_H("flt rule hdl0=0x%x, status=0x%x\n", m_pFilteringTable->rules[0].flt_rule_hdl, m_pFilteringTable->rules[0].status);
		}

		/* copy filter hdls */
		dft_v6fl_rule_hdl[IPV6_DEFAULT_FILTERTING_RULES] = m_pFilteringTable->rules[0].flt_rule_hdl;
		free(m_pFilteringTable);
	}

	return IPACM_SUCCESS;
}

int IPACM_Lan::handle_wan_up_ex(ipacm_ext_prop *ext_prop, ipa_ip_type iptype, uint8_t xlat_mux_id)
{
	int fd, ret = IPACM_SUCCESS, cnt;
	IPACM_Config* ipacm_config = IPACM_Iface::ipacmcfg;
	struct ipa_ioc_write_qmapid mux;

	if(rx_prop != NULL)
	{
		/* give mud ID to IPA-driver for WLAN/LAN pkts */
		fd = open(IPA_DEVICE_NAME, O_RDWR);
		if (0 == fd)
		{
			IPACMDBG_H("Failed opening %s.\n", IPA_DEVICE_NAME);
			return IPACM_FAILURE;
		}

		mux.qmap_id = ipacm_config->GetQmapId();
		for(cnt=0; cnt<rx_prop->num_rx_props; cnt++)
		{
			mux.client = rx_prop->rx[cnt].src_pipe;
			ret = ioctl(fd, IPA_IOC_WRITE_QMAPID, &mux);
			if (ret)
			{
				IPACMERR("Failed to write mux id %d\n", mux.qmap_id);
				close(fd);
				return IPACM_FAILURE;
			}
		}
		close(fd);
	}

	/* check only add static UL filter rule once */
	if ((num_dft_rt_v6 ==1 && iptype ==IPA_IP_v6) ||
			(iptype ==IPA_IP_v4))
	{
		IPACMDBG_H("num_dft_rt_v6 %d iptype %d xlat_mux_id: %d \n", num_dft_rt_v6, iptype, xlat_mux_id);
		ret = handle_uplink_filter_rule(ext_prop, iptype, xlat_mux_id);
	}
	return ret;
}

/* handle ETH client initial, construct full headers (tx property) */
int IPACM_Lan::handle_eth_hdr_init(uint8_t *mac_addr)
{

#define ETH_IFACE_INDEX_LEN 2

	int res = IPACM_SUCCESS, len = 0;
	char index[ETH_IFACE_INDEX_LEN];
	struct ipa_ioc_copy_hdr sCopyHeader;
	struct ipa_ioc_add_hdr *pHeaderDescriptor = NULL;
	uint32_t cnt;
	int clnt_indx;

	clnt_indx = get_eth_client_index(mac_addr);

	if (clnt_indx != IPACM_INVALID_INDEX)
	{
		IPACMERR("eth client is found/attached already with index %d \n", clnt_indx);
		return IPACM_FAILURE;
	}

	/* add header to IPA */
	if (num_eth_client >= IPA_MAX_NUM_ETH_CLIENTS)
	{
		IPACMERR("Reached maximum number(%d) of eth clients\n", IPA_MAX_NUM_ETH_CLIENTS);
		return IPACM_FAILURE;
	}

	IPACMDBG_H("ETH client number: %d\n", num_eth_client);

	memcpy(get_client_memptr(eth_client, num_eth_client)->mac,
				 mac_addr,
				 sizeof(get_client_memptr(eth_client, num_eth_client)->mac));


	IPACMDBG_H("Received Client MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
					 mac_addr[0], mac_addr[1], mac_addr[2],
					 mac_addr[3], mac_addr[4], mac_addr[5]);

	IPACMDBG_H("stored MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
					 get_client_memptr(eth_client, num_eth_client)->mac[0],
					 get_client_memptr(eth_client, num_eth_client)->mac[1],
					 get_client_memptr(eth_client, num_eth_client)->mac[2],
					 get_client_memptr(eth_client, num_eth_client)->mac[3],
					 get_client_memptr(eth_client, num_eth_client)->mac[4],
					 get_client_memptr(eth_client, num_eth_client)->mac[5]);

	/* add header to IPA */
	if(tx_prop != NULL)
	{
		len = sizeof(struct ipa_ioc_add_hdr) + (1 * sizeof(struct ipa_hdr_add));
		pHeaderDescriptor = (struct ipa_ioc_add_hdr *)calloc(1, len);
		if (pHeaderDescriptor == NULL)
		{
			IPACMERR("calloc failed to allocate pHeaderDescriptor\n");
			return IPACM_FAILURE;
		}

		/* copy partial header for v4*/
		for (cnt=0; cnt<tx_prop->num_tx_props; cnt++)
		{
				 if(tx_prop->tx[cnt].ip==IPA_IP_v4)
				 {
								IPACMDBG_H("Got partial v4-header name from %d tx props\n", cnt);
								memset(&sCopyHeader, 0, sizeof(sCopyHeader));
								memcpy(sCopyHeader.name,
											 tx_prop->tx[cnt].hdr_name,
											 sizeof(sCopyHeader.name));

								IPACMDBG_H("header name: %s in tx:%d\n", sCopyHeader.name,cnt);
								if (m_header.CopyHeader(&sCopyHeader) == false)
								{
									PERROR("ioctl copy header failed");
									res = IPACM_FAILURE;
									goto fail;
								}

								IPACMDBG_H("header length: %d, paritial: %d\n", sCopyHeader.hdr_len, sCopyHeader.is_partial);
								IPACMDBG_H("header eth2_ofst_valid: %d, eth2_ofst: %d\n", sCopyHeader.is_eth2_ofst_valid, sCopyHeader.eth2_ofst);
								if (sCopyHeader.hdr_len > IPA_HDR_MAX_SIZE)
								{
									IPACMERR("header oversize\n");
									res = IPACM_FAILURE;
									goto fail;
								}
								else
								{
									memcpy(pHeaderDescriptor->hdr[0].hdr,
												 sCopyHeader.hdr,
												 sCopyHeader.hdr_len);
								}

								/* copy client mac_addr to partial header */
								if (sCopyHeader.is_eth2_ofst_valid)
								{
									memcpy(&pHeaderDescriptor->hdr[0].hdr[sCopyHeader.eth2_ofst],
											 mac_addr,
											 IPA_MAC_ADDR_SIZE);
								}

								pHeaderDescriptor->commit = true;
								pHeaderDescriptor->num_hdrs = 1;

								memset(pHeaderDescriptor->hdr[0].name, 0,
											 sizeof(pHeaderDescriptor->hdr[0].name));

								snprintf(index,sizeof(index), "%d", ipa_if_num);
								strlcpy(pHeaderDescriptor->hdr[0].name, index, sizeof(pHeaderDescriptor->hdr[0].name));
								pHeaderDescriptor->hdr[0].name[IPA_RESOURCE_NAME_MAX-1] = '\0';
								if (strlcat(pHeaderDescriptor->hdr[0].name, IPA_ETH_HDR_NAME_v4, sizeof(pHeaderDescriptor->hdr[0].name)) > IPA_RESOURCE_NAME_MAX)
								{
									IPACMERR(" header name construction failed exceed length (%d)\n", strlen(pHeaderDescriptor->hdr[0].name));
									res = IPACM_FAILURE;
									goto fail;
								}

								snprintf(index,sizeof(index), "%d", header_name_count);
								if (strlcat(pHeaderDescriptor->hdr[0].name, index, sizeof(pHeaderDescriptor->hdr[0].name)) > IPA_RESOURCE_NAME_MAX)
								{
									IPACMERR(" header name construction failed exceed length (%d)\n", strlen(pHeaderDescriptor->hdr[0].name));
									res = IPACM_FAILURE;
									goto fail;
								}

								pHeaderDescriptor->hdr[0].hdr_len = sCopyHeader.hdr_len;
								pHeaderDescriptor->hdr[0].hdr_hdl = -1;
								pHeaderDescriptor->hdr[0].is_partial = 0;
								pHeaderDescriptor->hdr[0].status = -1;

					 if (m_header.AddHeader(pHeaderDescriptor) == false ||
							pHeaderDescriptor->hdr[0].status != 0)
					 {
						IPACMERR("ioctl IPA_IOC_ADD_HDR failed: %d\n", pHeaderDescriptor->hdr[0].status);
						res = IPACM_FAILURE;
						goto fail;
					 }

					get_client_memptr(eth_client, num_eth_client)->hdr_hdl_v4 = pHeaderDescriptor->hdr[0].hdr_hdl;
					IPACMDBG_H("eth-client(%d) v4 full header name:%s header handle:(0x%x)\n",
												 num_eth_client,
												 pHeaderDescriptor->hdr[0].name,
												 get_client_memptr(eth_client, num_eth_client)->hdr_hdl_v4);
									get_client_memptr(eth_client, num_eth_client)->ipv4_header_set=true;

					break;
				 }
		}


		/* copy partial header for v6*/
		for (cnt=0; cnt<tx_prop->num_tx_props; cnt++)
		{
			if(tx_prop->tx[cnt].ip==IPA_IP_v6)
			{

				IPACMDBG_H("Got partial v6-header name from %d tx props\n", cnt);
				memset(&sCopyHeader, 0, sizeof(sCopyHeader));
				memcpy(sCopyHeader.name,
						tx_prop->tx[cnt].hdr_name,
							sizeof(sCopyHeader.name));

				IPACMDBG_H("header name: %s in tx:%d\n", sCopyHeader.name,cnt);
				if (m_header.CopyHeader(&sCopyHeader) == false)
				{
					PERROR("ioctl copy header failed");
					res = IPACM_FAILURE;
					goto fail;
				}

				IPACMDBG_H("header length: %d, paritial: %d\n", sCopyHeader.hdr_len, sCopyHeader.is_partial);
				IPACMDBG_H("header eth2_ofst_valid: %d, eth2_ofst: %d\n", sCopyHeader.is_eth2_ofst_valid, sCopyHeader.eth2_ofst);
				if (sCopyHeader.hdr_len > IPA_HDR_MAX_SIZE)
				{
					IPACMERR("header oversize\n");
					res = IPACM_FAILURE;
					goto fail;
				}
				else
				{
					memcpy(pHeaderDescriptor->hdr[0].hdr,
							sCopyHeader.hdr,
								sCopyHeader.hdr_len);
				}

				/* copy client mac_addr to partial header */
				if (sCopyHeader.is_eth2_ofst_valid)
				{
					memcpy(&pHeaderDescriptor->hdr[0].hdr[sCopyHeader.eth2_ofst],
						mac_addr,
						IPA_MAC_ADDR_SIZE);
				}
				pHeaderDescriptor->commit = true;
				pHeaderDescriptor->num_hdrs = 1;

				memset(pHeaderDescriptor->hdr[0].name, 0,
					 sizeof(pHeaderDescriptor->hdr[0].name));

				snprintf(index,sizeof(index), "%d", ipa_if_num);
				strlcpy(pHeaderDescriptor->hdr[0].name, index, sizeof(pHeaderDescriptor->hdr[0].name));
				pHeaderDescriptor->hdr[0].name[IPA_RESOURCE_NAME_MAX-1] = '\0';
				if (strlcat(pHeaderDescriptor->hdr[0].name, IPA_ETH_HDR_NAME_v6, sizeof(pHeaderDescriptor->hdr[0].name)) > IPA_RESOURCE_NAME_MAX)
				{
					IPACMERR(" header name construction failed exceed length (%d)\n", strlen(pHeaderDescriptor->hdr[0].name));
					res = IPACM_FAILURE;
					goto fail;
				}
				snprintf(index,sizeof(index), "%d", header_name_count);
				if (strlcat(pHeaderDescriptor->hdr[0].name, index, sizeof(pHeaderDescriptor->hdr[0].name)) > IPA_RESOURCE_NAME_MAX)
				{
					IPACMERR(" header name construction failed exceed length (%d)\n", strlen(pHeaderDescriptor->hdr[0].name));
					res = IPACM_FAILURE;
					goto fail;
				}

				pHeaderDescriptor->hdr[0].hdr_len = sCopyHeader.hdr_len;
				pHeaderDescriptor->hdr[0].hdr_hdl = -1;
				pHeaderDescriptor->hdr[0].is_partial = 0;
				pHeaderDescriptor->hdr[0].status = -1;

				if (m_header.AddHeader(pHeaderDescriptor) == false ||
						pHeaderDescriptor->hdr[0].status != 0)
				{
					IPACMERR("ioctl IPA_IOC_ADD_HDR failed: %d\n", pHeaderDescriptor->hdr[0].status);
					res = IPACM_FAILURE;
					goto fail;
				}

				get_client_memptr(eth_client, num_eth_client)->hdr_hdl_v6 = pHeaderDescriptor->hdr[0].hdr_hdl;
				IPACMDBG_H("eth-client(%d) v6 full header name:%s header handle:(0x%x)\n",
						 num_eth_client,
						 pHeaderDescriptor->hdr[0].name,
									 get_client_memptr(eth_client, num_eth_client)->hdr_hdl_v6);

									get_client_memptr(eth_client, num_eth_client)->ipv6_header_set=true;

				break;

			}
		}
		/* initialize wifi client*/
		get_client_memptr(eth_client, num_eth_client)->route_rule_set_v4 = false;
		get_client_memptr(eth_client, num_eth_client)->route_rule_set_v6 = 0;
		get_client_memptr(eth_client, num_eth_client)->ipv4_set = false;
		get_client_memptr(eth_client, num_eth_client)->ipv6_set = 0;
		num_eth_client++;
		header_name_count++; //keep increasing header_name_count
		res = IPACM_SUCCESS;
		IPACMDBG_H("eth client number: %d\n", num_eth_client);
	}
	else
	{
		return res;
	}
fail:
	free(pHeaderDescriptor);
	return res;
}

/*handle eth client */
int IPACM_Lan::handle_eth_client_ipaddr(ipacm_event_data_all *data)
{
	int clnt_indx;
	int v6_num;

	IPACMDBG_H("number of eth clients: %d\n", num_eth_client);
	IPACMDBG_H("event MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
					 data->mac_addr[0],
					 data->mac_addr[1],
					 data->mac_addr[2],
					 data->mac_addr[3],
					 data->mac_addr[4],
					 data->mac_addr[5]);

	clnt_indx = get_eth_client_index(data->mac_addr);

		if (clnt_indx == IPACM_INVALID_INDEX)
		{
			IPACMERR("eth client not found/attached \n");
			return IPACM_FAILURE;
		}

	IPACMDBG_H("Ip-type received %d\n", data->iptype);
	if (data->iptype == IPA_IP_v4)
	{
		IPACMDBG_H("ipv4 address: 0x%x\n", data->ipv4_addr);
		if (data->ipv4_addr != 0) /* not 0.0.0.0 */
		{
			if (get_client_memptr(eth_client, clnt_indx)->ipv4_set == false)
			{
				get_client_memptr(eth_client, clnt_indx)->v4_addr = data->ipv4_addr;
				get_client_memptr(eth_client, clnt_indx)->ipv4_set = true;
			}
			else
			{
			   /* check if client got new IPv4 address*/
			   if(data->ipv4_addr == get_client_memptr(eth_client, clnt_indx)->v4_addr)
			   {
				IPACMDBG_H("Already setup ipv4 addr for client:%d, ipv4 address didn't change\n", clnt_indx);
				 return IPACM_FAILURE;
			   }
			   else
			   {
					IPACMDBG_H("ipv4 addr for client:%d is changed \n", clnt_indx);
					/* delete NAT rules first */
					CtList->HandleNeighIpAddrDelEvt(get_client_memptr(eth_client, clnt_indx)->v4_addr);
					delete_eth_rtrules(clnt_indx,IPA_IP_v4);
					get_client_memptr(eth_client, clnt_indx)->route_rule_set_v4 = false;
					get_client_memptr(eth_client, clnt_indx)->v4_addr = data->ipv4_addr;
				}
		}
	}
	else
	{
		    IPACMDBG_H("Invalid client IPv4 address \n");
		    return IPACM_FAILURE;
		}
	}
	else
	{
		if ((data->ipv6_addr[0] != 0) || (data->ipv6_addr[1] != 0) ||
				(data->ipv6_addr[2] != 0) || (data->ipv6_addr[3] || 0)) /* check if all 0 not valid ipv6 address */
		{
		   IPACMDBG_H("ipv6 address: 0x%x:%x:%x:%x\n", data->ipv6_addr[0], data->ipv6_addr[1], data->ipv6_addr[2], data->ipv6_addr[3]);
                   if(get_client_memptr(eth_client, clnt_indx)->ipv6_set < IPV6_NUM_ADDR)
		   {

		       for(v6_num=0;v6_num < get_client_memptr(eth_client, clnt_indx)->ipv6_set;v6_num++)
	               {
			      if( data->ipv6_addr[0] == get_client_memptr(eth_client, clnt_indx)->v6_addr[v6_num][0] &&
			           data->ipv6_addr[1] == get_client_memptr(eth_client, clnt_indx)->v6_addr[v6_num][1] &&
			  	        data->ipv6_addr[2]== get_client_memptr(eth_client, clnt_indx)->v6_addr[v6_num][2] &&
			  	         data->ipv6_addr[3] == get_client_memptr(eth_client, clnt_indx)->v6_addr[v6_num][3])
			      {
					IPACMDBG_H("Already see this ipv6 addr at position: %d for client:%d\n", v6_num, clnt_indx);
			  	    return IPACM_FAILURE; /* not setup the RT rules*/
			      }
		       }

		       /* not see this ipv6 before for wifi client*/
			   get_client_memptr(eth_client, clnt_indx)->v6_addr[get_client_memptr(eth_client, clnt_indx)->ipv6_set][0] = data->ipv6_addr[0];
			   get_client_memptr(eth_client, clnt_indx)->v6_addr[get_client_memptr(eth_client, clnt_indx)->ipv6_set][1] = data->ipv6_addr[1];
			   get_client_memptr(eth_client, clnt_indx)->v6_addr[get_client_memptr(eth_client, clnt_indx)->ipv6_set][2] = data->ipv6_addr[2];
			   get_client_memptr(eth_client, clnt_indx)->v6_addr[get_client_memptr(eth_client, clnt_indx)->ipv6_set][3] = data->ipv6_addr[3];
			   get_client_memptr(eth_client, clnt_indx)->ipv6_set++;
		    }
		    else
		    {
		         IPACMDBG_H("Already got 3 ipv6 addr for client:%d\n", clnt_indx);
			 return IPACM_FAILURE; /* not setup the RT rules*/
		    }
		}
	}

	return IPACM_SUCCESS;
}

/*handle eth client routing rule*/
int IPACM_Lan::handle_eth_client_route_rule(uint8_t *mac_addr, ipa_ip_type iptype)
{
	struct ipa_ioc_add_rt_rule *rt_rule;
	struct ipa_rt_rule_add *rt_rule_entry;
	uint32_t tx_index;
	int eth_index,v6_num;
	const int NUM = 1;

	if(tx_prop == NULL)
	{
		IPACMDBG_H("No rx properties registered for iface %s\n", dev_name);
		return IPACM_SUCCESS;
	}

	IPACMDBG_H("Received mac_addr MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
					 mac_addr[0], mac_addr[1], mac_addr[2],
					 mac_addr[3], mac_addr[4], mac_addr[5]);

	eth_index = get_eth_client_index(mac_addr);
	if (eth_index == IPACM_INVALID_INDEX)
	{
		IPACMDBG_H("eth client not found/attached \n");
		return IPACM_SUCCESS;
	}

	if (iptype==IPA_IP_v4) {
		IPACMDBG_H("eth client index: %d, ip-type: %d, ipv4_set:%d, ipv4_rule_set:%d \n", eth_index, iptype,
					 get_client_memptr(eth_client, eth_index)->ipv4_set,
					 get_client_memptr(eth_client, eth_index)->route_rule_set_v4);
	} else {
		IPACMDBG_H("eth client index: %d, ip-type: %d, ipv6_set:%d, ipv6_rule_num:%d \n", eth_index, iptype,
					 get_client_memptr(eth_client, eth_index)->ipv6_set,
					 get_client_memptr(eth_client, eth_index)->route_rule_set_v6);
	}
	/* Add default routing rules if not set yet */
	if ((iptype == IPA_IP_v4
			 && get_client_memptr(eth_client, eth_index)->route_rule_set_v4 == false
			 && get_client_memptr(eth_client, eth_index)->ipv4_set == true)
			|| (iptype == IPA_IP_v6
		            && get_client_memptr(eth_client, eth_index)->route_rule_set_v6 < get_client_memptr(eth_client, eth_index)->ipv6_set
					))
	{

        /* Add corresponding ipa_rm_resource_name of TX-endpoint up before IPV6 RT-rule set */
		IPACMDBG_H("dev %s add producer dependency\n", dev_name);
		if (tx_prop != NULL)
		{
			IPACMDBG_H("depend Got pipe %d rm index : %d \n", tx_prop->tx[0].dst_pipe, IPACM_Iface::ipacmcfg->ipa_client_rm_map_tbl[tx_prop->tx[0].dst_pipe]);
			IPACM_Iface::ipacmcfg->AddRmDepend(IPACM_Iface::ipacmcfg->ipa_client_rm_map_tbl[tx_prop->tx[0].dst_pipe],false);
		}
		rt_rule = (struct ipa_ioc_add_rt_rule *)
			 calloc(1, sizeof(struct ipa_ioc_add_rt_rule) +
						NUM * sizeof(struct ipa_rt_rule_add));

		if (rt_rule == NULL)
		{
			PERROR("Error Locate ipa_ioc_add_rt_rule memory...\n");
			return IPACM_FAILURE;
		}

		rt_rule->commit = 1;
		rt_rule->num_rules = (uint8_t)NUM;
		rt_rule->ip = iptype;

		for (tx_index = 0; tx_index < iface_query->num_tx_props; tx_index++)
		{
			if(iptype != tx_prop->tx[tx_index].ip)
		    {
				IPACMDBG_H("Tx:%d, ip-type: %d conflict ip-type: %d no RT-rule added\n",
						tx_index, tx_prop->tx[tx_index].ip,iptype);
		   	        continue;
		    }

  	   	    rt_rule_entry = &rt_rule->rules[0];
			rt_rule_entry->at_rear = 0;

			if (iptype == IPA_IP_v4)
			{
		        IPACMDBG_H("client index(%d):ipv4 address: 0x%x\n", eth_index,
		  		        get_client_memptr(eth_client, eth_index)->v4_addr);

                IPACMDBG_H("client(%d): v4 header handle:(0x%x)\n",
		  				 eth_index,
		  				 get_client_memptr(eth_client, eth_index)->hdr_hdl_v4);
				strlcpy(rt_rule->rt_tbl_name,
								IPACM_Iface::ipacmcfg->rt_tbl_lan_v4.name,
								sizeof(rt_rule->rt_tbl_name));
				rt_rule->rt_tbl_name[IPA_RESOURCE_NAME_MAX-1] = '\0';
			    rt_rule_entry->rule.dst = tx_prop->tx[tx_index].dst_pipe;
			    memcpy(&rt_rule_entry->rule.attrib,
						 &tx_prop->tx[tx_index].attrib,
						 sizeof(rt_rule_entry->rule.attrib));
			    rt_rule_entry->rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
		   	    rt_rule_entry->rule.hdr_hdl = get_client_memptr(eth_client, eth_index)->hdr_hdl_v4;
				rt_rule_entry->rule.attrib.u.v4.dst_addr = get_client_memptr(eth_client, eth_index)->v4_addr;
				rt_rule_entry->rule.attrib.u.v4.dst_addr_mask = 0xFFFFFFFF;
			    if (false == m_routing.AddRoutingRule(rt_rule))
  	            {
  	          	            IPACMERR("Routing rule addition failed!\n");
  	          	            free(rt_rule);
  	          	            return IPACM_FAILURE;
			    }

			    /* copy ipv4 RT hdl */
		        get_client_memptr(eth_client, eth_index)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v4 =
  	   	        rt_rule->rules[0].rt_rule_hdl;
		        IPACMDBG_H("tx:%d, rt rule hdl=%x ip-type: %d\n", tx_index,
		      	get_client_memptr(eth_client, eth_index)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v4, iptype);

  	   	    } else {

		        for(v6_num = get_client_memptr(eth_client, eth_index)->route_rule_set_v6;v6_num < get_client_memptr(eth_client, eth_index)->ipv6_set;v6_num++)
			    {
                    IPACMDBG_H("client(%d): v6 header handle:(0x%x)\n",
		  	    			 eth_index,
		  	    			 get_client_memptr(eth_client, eth_index)->hdr_hdl_v6);

		            /* v6 LAN_RT_TBL */
				strlcpy(rt_rule->rt_tbl_name,
			    					IPACM_Iface::ipacmcfg->rt_tbl_v6.name,
			    					sizeof(rt_rule->rt_tbl_name));
				rt_rule->rt_tbl_name[IPA_RESOURCE_NAME_MAX-1] = '\0';
		            /* Support QCMAP LAN traffic feature, send to A5 */
					rt_rule_entry->rule.dst = IPA_CLIENT_APPS_LAN_CONS;
			        memset(&rt_rule_entry->rule.attrib, 0, sizeof(rt_rule_entry->rule.attrib));
		   	        rt_rule_entry->rule.hdr_hdl = 0;
			        rt_rule_entry->rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
		   	        rt_rule_entry->rule.attrib.u.v6.dst_addr[0] = get_client_memptr(eth_client, eth_index)->v6_addr[v6_num][0];
		   	        rt_rule_entry->rule.attrib.u.v6.dst_addr[1] = get_client_memptr(eth_client, eth_index)->v6_addr[v6_num][1];
		   	        rt_rule_entry->rule.attrib.u.v6.dst_addr[2] = get_client_memptr(eth_client, eth_index)->v6_addr[v6_num][2];
		   	        rt_rule_entry->rule.attrib.u.v6.dst_addr[3] = get_client_memptr(eth_client, eth_index)->v6_addr[v6_num][3];
					rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[0] = 0xFFFFFFFF;
					rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[1] = 0xFFFFFFFF;
					rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[2] = 0xFFFFFFFF;
					rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[3] = 0xFFFFFFFF;
   	                if (false == m_routing.AddRoutingRule(rt_rule))
  	                {
  	                	    IPACMERR("Routing rule addition failed!\n");
  	                	    free(rt_rule);
  	                	    return IPACM_FAILURE;
			        }

		            get_client_memptr(eth_client, eth_index)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6[v6_num] = rt_rule->rules[0].rt_rule_hdl;
		            IPACMDBG_H("tx:%d, rt rule hdl=%x ip-type: %d\n", tx_index,
		            				 get_client_memptr(eth_client, eth_index)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6[v6_num], iptype);

			        /*Copy same rule to v6 WAN RT TBL*/
				strlcpy(rt_rule->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_wan_v6.name, sizeof(rt_rule->rt_tbl_name));
				rt_rule->rt_tbl_name[IPA_RESOURCE_NAME_MAX-1] = '\0';
				/* Downlink traffic from Wan iface, directly through IPA */
					rt_rule_entry->rule.dst = tx_prop->tx[tx_index].dst_pipe;
			        memcpy(&rt_rule_entry->rule.attrib,
						 &tx_prop->tx[tx_index].attrib,
						 sizeof(rt_rule_entry->rule.attrib));
		   	        rt_rule_entry->rule.hdr_hdl = get_client_memptr(eth_client, eth_index)->hdr_hdl_v6;
			        rt_rule_entry->rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
		   	        rt_rule_entry->rule.attrib.u.v6.dst_addr[0] = get_client_memptr(eth_client, eth_index)->v6_addr[v6_num][0];
		   	        rt_rule_entry->rule.attrib.u.v6.dst_addr[1] = get_client_memptr(eth_client, eth_index)->v6_addr[v6_num][1];
		   	        rt_rule_entry->rule.attrib.u.v6.dst_addr[2] = get_client_memptr(eth_client, eth_index)->v6_addr[v6_num][2];
		   	        rt_rule_entry->rule.attrib.u.v6.dst_addr[3] = get_client_memptr(eth_client, eth_index)->v6_addr[v6_num][3];
					rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[0] = 0xFFFFFFFF;
					rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[1] = 0xFFFFFFFF;
					rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[2] = 0xFFFFFFFF;
					rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[3] = 0xFFFFFFFF;
		            if (false == m_routing.AddRoutingRule(rt_rule))
		            {
							IPACMERR("Routing rule addition failed!\n");
							free(rt_rule);
							return IPACM_FAILURE;
		            }

		            get_client_memptr(eth_client, eth_index)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6_wan[v6_num] = rt_rule->rules[0].rt_rule_hdl;
					IPACMDBG_H("tx:%d, rt rule hdl=%x ip-type: %d\n", tx_index,
		            				 get_client_memptr(eth_client, eth_index)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6_wan[v6_num], iptype);
			    }
			}

  	    } /* end of for loop */

		free(rt_rule);

		if (iptype == IPA_IP_v4)
		{
			get_client_memptr(eth_client, eth_index)->route_rule_set_v4 = true;
		}
		else
		{
			get_client_memptr(eth_client, eth_index)->route_rule_set_v6 = get_client_memptr(eth_client, eth_index)->ipv6_set;
		}
	}
	return IPACM_SUCCESS;
}

/* handle odu client initial, construct full headers (tx property) */
int IPACM_Lan::handle_odu_hdr_init(uint8_t *mac_addr)
{
	int res = IPACM_SUCCESS, len = 0;
	struct ipa_ioc_copy_hdr sCopyHeader;
	struct ipa_ioc_add_hdr *pHeaderDescriptor = NULL;
	uint32_t cnt;

	IPACMDBG("Received Client MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
					 mac_addr[0], mac_addr[1], mac_addr[2],
					 mac_addr[3], mac_addr[4], mac_addr[5]);

	/* add header to IPA */
	if(tx_prop != NULL)
	{
		len = sizeof(struct ipa_ioc_add_hdr) + (1 * sizeof(struct ipa_hdr_add));
		pHeaderDescriptor = (struct ipa_ioc_add_hdr *)calloc(1, len);
		if (pHeaderDescriptor == NULL)
		{
			IPACMERR("calloc failed to allocate pHeaderDescriptor\n");
			return IPACM_FAILURE;
		}

		/* copy partial header for v4*/
		for (cnt=0; cnt<tx_prop->num_tx_props; cnt++)
		{
				 if(tx_prop->tx[cnt].ip==IPA_IP_v4)
				 {
								IPACMDBG("Got partial v4-header name from %d tx props\n", cnt);
								memset(&sCopyHeader, 0, sizeof(sCopyHeader));
								memcpy(sCopyHeader.name,
											tx_prop->tx[cnt].hdr_name,
											 sizeof(sCopyHeader.name));
								IPACMDBG("header name: %s in tx:%d\n", sCopyHeader.name,cnt);
								if (m_header.CopyHeader(&sCopyHeader) == false)
								{
									PERROR("ioctl copy header failed");
									res = IPACM_FAILURE;
									goto fail;
								}
								IPACMDBG("header length: %d, paritial: %d\n", sCopyHeader.hdr_len, sCopyHeader.is_partial);
								if (sCopyHeader.hdr_len > IPA_HDR_MAX_SIZE)
								{
									IPACMERR("header oversize\n");
									res = IPACM_FAILURE;
									goto fail;
								}
								else
								{
									memcpy(pHeaderDescriptor->hdr[0].hdr,
												 sCopyHeader.hdr,
												 sCopyHeader.hdr_len);
								}
								/* copy client mac_addr to partial header */
								if (sCopyHeader.is_eth2_ofst_valid)
								{
									memcpy(&pHeaderDescriptor->hdr[0].hdr[sCopyHeader.eth2_ofst],
											 mac_addr,
											 IPA_MAC_ADDR_SIZE);
								}


								pHeaderDescriptor->commit = true;
								pHeaderDescriptor->num_hdrs = 1;

								memset(pHeaderDescriptor->hdr[0].name, 0,
											 sizeof(pHeaderDescriptor->hdr[0].name));
								strcpy(pHeaderDescriptor->hdr[0].name, IPA_ODU_HDR_NAME_v4);
								pHeaderDescriptor->hdr[0].hdr_len = sCopyHeader.hdr_len;
								pHeaderDescriptor->hdr[0].hdr_hdl = -1;
								pHeaderDescriptor->hdr[0].is_partial = 0;
								pHeaderDescriptor->hdr[0].status = -1;

					 if (m_header.AddHeader(pHeaderDescriptor) == false ||
							pHeaderDescriptor->hdr[0].status != 0)
					 {
						IPACMERR("ioctl IPA_IOC_ADD_HDR failed: %d\n", pHeaderDescriptor->hdr[0].status);
						res = IPACM_FAILURE;
						goto fail;
					 }

					ODU_hdr_hdl_v4 = pHeaderDescriptor->hdr[0].hdr_hdl;
					ipv4_header_set = true ;
					IPACMDBG(" ODU v4 full header name:%s header handle:(0x%x)\n",
										 pHeaderDescriptor->hdr[0].name,
												 ODU_hdr_hdl_v4);
					break;
				 }
		}


		/* copy partial header for v6*/
		for (cnt=0; cnt<tx_prop->num_tx_props; cnt++)
		{
			if(tx_prop->tx[cnt].ip==IPA_IP_v6)
			{

				IPACMDBG("Got partial v6-header name from %d tx props\n", cnt);
				memset(&sCopyHeader, 0, sizeof(sCopyHeader));
				memcpy(sCopyHeader.name,
						tx_prop->tx[cnt].hdr_name,
							sizeof(sCopyHeader.name));

				IPACMDBG("header name: %s in tx:%d\n", sCopyHeader.name,cnt);
				if (m_header.CopyHeader(&sCopyHeader) == false)
				{
					PERROR("ioctl copy header failed");
					res = IPACM_FAILURE;
					goto fail;
				}

				IPACMDBG("header length: %d, paritial: %d\n", sCopyHeader.hdr_len, sCopyHeader.is_partial);
				if (sCopyHeader.hdr_len > IPA_HDR_MAX_SIZE)
				{
					IPACMERR("header oversize\n");
					res = IPACM_FAILURE;
					goto fail;
				}
				else
				{
					memcpy(pHeaderDescriptor->hdr[0].hdr,
							sCopyHeader.hdr,
								sCopyHeader.hdr_len);
				}

				/* copy client mac_addr to partial header */
				if (sCopyHeader.is_eth2_ofst_valid)
				{
					memcpy(&pHeaderDescriptor->hdr[0].hdr[sCopyHeader.eth2_ofst],
					 mac_addr,
					 IPA_MAC_ADDR_SIZE);
				}

				pHeaderDescriptor->commit = true;
				pHeaderDescriptor->num_hdrs = 1;

				memset(pHeaderDescriptor->hdr[0].name, 0,
					 sizeof(pHeaderDescriptor->hdr[0].name));

				strcpy(pHeaderDescriptor->hdr[0].name, IPA_ODU_HDR_NAME_v6);
				pHeaderDescriptor->hdr[0].hdr_len = sCopyHeader.hdr_len;
				pHeaderDescriptor->hdr[0].hdr_hdl = -1;
				pHeaderDescriptor->hdr[0].is_partial = 0;
				pHeaderDescriptor->hdr[0].status = -1;

				if (m_header.AddHeader(pHeaderDescriptor) == false ||
						pHeaderDescriptor->hdr[0].status != 0)
				{
					IPACMERR("ioctl IPA_IOC_ADD_HDR failed: %d\n", pHeaderDescriptor->hdr[0].status);
					res = IPACM_FAILURE;
					goto fail;
				}

				ODU_hdr_hdl_v6 = pHeaderDescriptor->hdr[0].hdr_hdl;
				ipv6_header_set = true ;
				IPACMDBG(" ODU v4 full header name:%s header handle:(0x%x)\n",
									 pHeaderDescriptor->hdr[0].name,
											 ODU_hdr_hdl_v6);
				break;
			}
		}
	}
fail:
	free(pHeaderDescriptor);
	return res;
}


/* handle odu default route rule configuration */
int IPACM_Lan::handle_odu_route_add()
{
	/* add default WAN route */
	struct ipa_ioc_add_rt_rule *rt_rule;
	struct ipa_rt_rule_add *rt_rule_entry;
	uint32_t tx_index;
	const int NUM = 1;

	if(tx_prop == NULL)
	{
	  IPACMDBG_H("No tx properties, ignore default route setting\n");
	  return IPACM_SUCCESS;
	}

	rt_rule = (struct ipa_ioc_add_rt_rule *)
		 calloc(1, sizeof(struct ipa_ioc_add_rt_rule) +
						NUM * sizeof(struct ipa_rt_rule_add));

	if (!rt_rule)
	{
		IPACMERR("Error Locate ipa_ioc_add_rt_rule memory...\n");
		return IPACM_FAILURE;
	}

	rt_rule->commit = 1;
	rt_rule->num_rules = (uint8_t)NUM;


	IPACMDBG_H("WAN table created %s \n", rt_rule->rt_tbl_name);
	rt_rule_entry = &rt_rule->rules[0];
	rt_rule_entry->at_rear = true;

	for (tx_index = 0; tx_index < iface_query->num_tx_props; tx_index++)
	{

		if (IPA_IP_v4 == tx_prop->tx[tx_index].ip)
		{
			strcpy(rt_rule->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_odu_v4.name);
			rt_rule_entry->rule.hdr_hdl = ODU_hdr_hdl_v4;
			rt_rule->ip = IPA_IP_v4;
		}
		else
		{
			strcpy(rt_rule->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_odu_v6.name);
			rt_rule_entry->rule.hdr_hdl = ODU_hdr_hdl_v6;
			rt_rule->ip = IPA_IP_v6;
		}

		rt_rule_entry->rule.dst = tx_prop->tx[tx_index].dst_pipe;
		memcpy(&rt_rule_entry->rule.attrib,
					 &tx_prop->tx[tx_index].attrib,
					 sizeof(rt_rule_entry->rule.attrib));

		rt_rule_entry->rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
		if (IPA_IP_v4 == tx_prop->tx[tx_index].ip)
		{
			rt_rule_entry->rule.attrib.u.v4.dst_addr      = 0;
			rt_rule_entry->rule.attrib.u.v4.dst_addr_mask = 0;
			if (false == m_routing.AddRoutingRule(rt_rule))
			{
				IPACMERR("Routing rule addition failed!\n");
				free(rt_rule);
				return IPACM_FAILURE;
			}
			odu_route_rule_v4_hdl[tx_index] = rt_rule_entry->rt_rule_hdl;
			IPACMDBG_H("Got ipv4 ODU-route rule hdl:0x%x,tx:%d,ip-type: %d \n",
						 odu_route_rule_v4_hdl[tx_index],
						 tx_index,
						 IPA_IP_v4);
		}
		else
		{
			rt_rule_entry->rule.attrib.u.v6.dst_addr[0] = 0;
			rt_rule_entry->rule.attrib.u.v6.dst_addr[1] = 0;
			rt_rule_entry->rule.attrib.u.v6.dst_addr[2] = 0;
			rt_rule_entry->rule.attrib.u.v6.dst_addr[3] = 0;
			rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[0] = 0;
			rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[1] = 0;
			rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[2] = 0;
			rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[3] = 0;
			if (false == m_routing.AddRoutingRule(rt_rule))
			{
				IPACMERR("Routing rule addition failed!\n");
				free(rt_rule);
				return IPACM_FAILURE;
			}
			odu_route_rule_v6_hdl[tx_index] = rt_rule_entry->rt_rule_hdl;
			IPACMDBG_H("Set ipv6 ODU-route rule hdl for v6_lan_table:0x%x,tx:%d,ip-type: %d \n",
					odu_route_rule_v6_hdl[tx_index],
					tx_index,
					IPA_IP_v6);
		}
	}
	free(rt_rule);
	return IPACM_SUCCESS;
}

/* handle odu default route rule deletion */
int IPACM_Lan::handle_odu_route_del()
{
	uint32_t tx_index;

	if(tx_prop == NULL)
	{
		IPACMDBG_H("No tx properties, ignore delete default route setting\n");
		return IPACM_SUCCESS;
	}

	for (tx_index = 0; tx_index < iface_query->num_tx_props; tx_index++)
	{
		if (tx_prop->tx[tx_index].ip == IPA_IP_v4)
		{
			IPACMDBG_H("Tx:%d, ip-type: %d match ip-type: %d, RT-rule deleted\n",
					tx_index, tx_prop->tx[tx_index].ip,IPA_IP_v4);

			if (m_routing.DeleteRoutingHdl(odu_route_rule_v4_hdl[tx_index], IPA_IP_v4)
					== false)
			{
				IPACMERR("IP-family:%d, Routing rule(hdl:0x%x) deletion failed with tx_index %d!\n", IPA_IP_v4, odu_route_rule_v4_hdl[tx_index], tx_index);
				return IPACM_FAILURE;
			}
		}
		else
		{
			IPACMDBG_H("Tx:%d, ip-type: %d match ip-type: %d, RT-rule deleted\n",
					tx_index, tx_prop->tx[tx_index].ip,IPA_IP_v6);

			if (m_routing.DeleteRoutingHdl(odu_route_rule_v6_hdl[tx_index], IPA_IP_v6)
					== false)
			{
				IPACMERR("IP-family:%d, Routing rule(hdl:0x%x) deletion failed with tx_index %d!\n", IPA_IP_v6, odu_route_rule_v6_hdl[tx_index], tx_index);
				return IPACM_FAILURE;
			}
		}
	}

	return IPACM_SUCCESS;
}

/*handle eth client del mode*/
int IPACM_Lan::handle_eth_client_down_evt(uint8_t *mac_addr)
{
	int clt_indx;
	uint32_t tx_index;
	int num_eth_client_tmp = num_eth_client;
	int num_v6;

	IPACMDBG_H("total client: %d\n", num_eth_client_tmp);

	clt_indx = get_eth_client_index(mac_addr);
	if (clt_indx == IPACM_INVALID_INDEX)
	{
		IPACMDBG_H("eth client not attached\n");
		return IPACM_SUCCESS;
	}

	/* First reset nat rules and then route rules */
	if(get_client_memptr(eth_client, clt_indx)->ipv4_set == true)
	{
			IPACMDBG_H("Clean Nat Rules for ipv4:0x%x\n", get_client_memptr(eth_client, clt_indx)->v4_addr);
			CtList->HandleNeighIpAddrDelEvt(get_client_memptr(eth_client, clt_indx)->v4_addr);
 	}

	if (delete_eth_rtrules(clt_indx, IPA_IP_v4))
	{
		IPACMERR("unbale to delete ecm-client v4 route rules for index: %d\n", clt_indx);
		return IPACM_FAILURE;
	}

	if (delete_eth_rtrules(clt_indx, IPA_IP_v6))
	{
		IPACMERR("unbale to delete ecm-client v6 route rules for index: %d\n", clt_indx);
		return IPACM_FAILURE;
	}

	/* Delete eth client header */
	if(get_client_memptr(eth_client, clt_indx)->ipv4_header_set == true)
	{
		if (m_header.DeleteHeaderHdl(get_client_memptr(eth_client, clt_indx)->hdr_hdl_v4)
				== false)
		{
			return IPACM_FAILURE;
		}
		get_client_memptr(eth_client, clt_indx)->ipv4_header_set = false;
	}

	if(get_client_memptr(eth_client, clt_indx)->ipv6_header_set == true)
	{
		if (m_header.DeleteHeaderHdl(get_client_memptr(eth_client, clt_indx)->hdr_hdl_v6)
				== false)
		{
			return IPACM_FAILURE;
		}
		get_client_memptr(eth_client, clt_indx)->ipv6_header_set = false;
	}

	/* Reset ip_set to 0*/
	get_client_memptr(eth_client, clt_indx)->ipv4_set = false;
	get_client_memptr(eth_client, clt_indx)->ipv6_set = 0;
	get_client_memptr(eth_client, clt_indx)->ipv4_header_set = false;
	get_client_memptr(eth_client, clt_indx)->ipv6_header_set = false;
	get_client_memptr(eth_client, clt_indx)->route_rule_set_v4 = false;
	get_client_memptr(eth_client, clt_indx)->route_rule_set_v6 = 0;

	for (; clt_indx < num_eth_client_tmp - 1; clt_indx++)
	{
		memcpy(get_client_memptr(eth_client, clt_indx)->mac,
					 get_client_memptr(eth_client, (clt_indx + 1))->mac,
					 sizeof(get_client_memptr(eth_client, clt_indx)->mac));

		get_client_memptr(eth_client, clt_indx)->hdr_hdl_v4 = get_client_memptr(eth_client, (clt_indx + 1))->hdr_hdl_v4;
		get_client_memptr(eth_client, clt_indx)->hdr_hdl_v6 = get_client_memptr(eth_client, (clt_indx + 1))->hdr_hdl_v6;
		get_client_memptr(eth_client, clt_indx)->v4_addr = get_client_memptr(eth_client, (clt_indx + 1))->v4_addr;

		get_client_memptr(eth_client, clt_indx)->ipv4_set = get_client_memptr(eth_client, (clt_indx + 1))->ipv4_set;
		get_client_memptr(eth_client, clt_indx)->ipv6_set = get_client_memptr(eth_client, (clt_indx + 1))->ipv6_set;
		get_client_memptr(eth_client, clt_indx)->ipv4_header_set = get_client_memptr(eth_client, (clt_indx + 1))->ipv4_header_set;
		get_client_memptr(eth_client, clt_indx)->ipv6_header_set = get_client_memptr(eth_client, (clt_indx + 1))->ipv6_header_set;

		get_client_memptr(eth_client, clt_indx)->route_rule_set_v4 = get_client_memptr(eth_client, (clt_indx + 1))->route_rule_set_v4;
		get_client_memptr(eth_client, clt_indx)->route_rule_set_v6 = get_client_memptr(eth_client, (clt_indx + 1))->route_rule_set_v6;

        for (num_v6=0;num_v6< get_client_memptr(eth_client, clt_indx)->ipv6_set;num_v6++)
	    {
		    get_client_memptr(eth_client, clt_indx)->v6_addr[num_v6][0] = get_client_memptr(eth_client, (clt_indx + 1))->v6_addr[num_v6][0];
		    get_client_memptr(eth_client, clt_indx)->v6_addr[num_v6][1] = get_client_memptr(eth_client, (clt_indx + 1))->v6_addr[num_v6][1];
		    get_client_memptr(eth_client, clt_indx)->v6_addr[num_v6][2] = get_client_memptr(eth_client, (clt_indx + 1))->v6_addr[num_v6][2];
		    get_client_memptr(eth_client, clt_indx)->v6_addr[num_v6][3] = get_client_memptr(eth_client, (clt_indx + 1))->v6_addr[num_v6][3];
        }

		for (tx_index = 0; tx_index < iface_query->num_tx_props; tx_index++)
		{
			get_client_memptr(eth_client, clt_indx)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v4 =
				 get_client_memptr(eth_client, (clt_indx + 1))->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v4;

			for(num_v6=0;num_v6< get_client_memptr(eth_client, clt_indx)->route_rule_set_v6;num_v6++)
			{
			  get_client_memptr(eth_client, clt_indx)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6[num_v6] =
			   	 get_client_memptr(eth_client, (clt_indx + 1))->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6[num_v6];
			  get_client_memptr(eth_client, clt_indx)->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6_wan[num_v6] =
			   	 get_client_memptr(eth_client, (clt_indx + 1))->eth_rt_hdl[tx_index].eth_rt_rule_hdl_v6_wan[num_v6];
		    }
		}
	}

	IPACMDBG_H(" %d eth client deleted successfully \n", num_eth_client);
	num_eth_client = num_eth_client - 1;
	IPACMDBG_H(" Number of eth client: %d\n", num_eth_client);

	/* Del RM dependency */
	if(num_eth_client == 0)
	{
		/* Delete corresponding ipa_rm_resource_name of TX-endpoint after delete all IPV4V6 RT-rule*/
		IPACMDBG_H("dev %s add producer dependency\n", dev_name);
		if (tx_prop != NULL)
		{
			IPACMDBG_H("depend Got pipe %d rm index : %d \n", tx_prop->tx[0].dst_pipe, IPACM_Iface::ipacmcfg->ipa_client_rm_map_tbl[tx_prop->tx[0].dst_pipe]);
			IPACM_Iface::ipacmcfg->DelRmDepend(IPACM_Iface::ipacmcfg->ipa_client_rm_map_tbl[tx_prop->tx[0].dst_pipe]);
		}
	}

	return IPACM_SUCCESS;
}

/*handle LAN iface down event*/
int IPACM_Lan::handle_down_evt()
{
	int i;
	int res = IPACM_SUCCESS;
	uint32_t temp_eth_bridge_flt_rule[IPA_LAN_TO_LAN_MAX_WLAN_CLIENT];

	IPACMDBG_H("lan handle_down_evt\n ");
	if (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
	{
		/* delete ODU default RT rules */
		if (IPACM_Iface::ipacmcfg->ipacm_odu_embms_enable == true)
		{
			IPACMDBG_H("eMBMS enable, delete eMBMS DL RT rule\n");
			handle_odu_route_del();
		}

		/* delete full header */
		if (ipv4_header_set)
		{
			if (m_header.DeleteHeaderHdl(ODU_hdr_hdl_v4)
					== false)
			{
					IPACMERR("ODU ipv4 header delete fail\n");
					res = IPACM_FAILURE;
					goto fail;
			}
			IPACMDBG_H("ODU ipv4 header delete success\n");
		}

		if (ipv6_header_set)
		{
			if (m_header.DeleteHeaderHdl(ODU_hdr_hdl_v6)
					== false)
			{
				IPACMERR("ODU ipv6 header delete fail\n");
				res = IPACM_FAILURE;
				goto fail;
			}
			IPACMERR("ODU ipv6 header delete success\n");
		}
	}

	/* no iface address up, directly close iface*/
	if (ip_type == IPACM_IP_NULL)
	{
		goto fail;
	}

#ifdef FEATURE_ETH_BRIDGE_LE
	if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
	{
		IPACM_Lan::usb_hdr_template_hdl = 0;
		IPACM_Lan::is_usb_up = false;
		if(IPACM_Lan::is_cpe_up == false) //if all LAN ifaces are down
		{
			IPACM_Lan::lan_hdr_type = IPA_HDR_L2_NONE;
		}
	}
	if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
	{
		IPACM_Lan::cpe_hdr_template_hdl = 0;
		IPACM_Lan::is_cpe_up = false;
		if(IPACM_Lan::is_usb_up == false) //if all LAN ifaces are down
		{
			IPACM_Lan::lan_hdr_type = IPA_HDR_L2_NONE;
		}
	}
	del_hdr_proc_ctx();
#endif

	/* delete wan filter rule */
	if (IPACM_Wan::isWanUP(ipa_if_num) && rx_prop != NULL)
	{
		IPACMDBG_H("LAN IF goes down, backhaul type %d\n", IPACM_Wan::backhaul_is_sta_mode);
		handle_wan_down(IPACM_Wan::backhaul_is_sta_mode);
	}

	if (IPACM_Wan::isWanUP_V6(ipa_if_num) && rx_prop != NULL)
	{
		IPACMDBG_H("LAN IF goes down, backhaul type %d\n", IPACM_Wan::backhaul_is_sta_mode);
		handle_wan_down_v6(IPACM_Wan::backhaul_is_sta_mode);
	}

	/* delete default filter rules */
	if (ip_type != IPA_IP_v6 && rx_prop != NULL)
	{
		if(m_filtering.DeleteFilteringHdls(ipv4_icmp_flt_rule_hdl, IPA_IP_v4, NUM_IPV4_ICMP_FLT_RULE) == false)
		{
			IPACMERR("Error Deleting ICMPv4 Filtering Rule, aborting...\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, NUM_IPV4_ICMP_FLT_RULE);

		if (m_filtering.DeleteFilteringHdls(dft_v4fl_rule_hdl, IPA_IP_v4, IPV4_DEFAULT_FILTERTING_RULES) == false)
		{
			IPACMERR("Error Deleting Filtering Rule, aborting...\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, IPV4_DEFAULT_FILTERTING_RULES);
#ifdef FEATURE_ETH_BRIDGE_LE
		for(i=0; i<IPA_LAN_TO_LAN_MAX_WLAN_CLIENT; i++)
		{
			temp_eth_bridge_flt_rule[i] = wlan_client_flt_rule_hdl_v4[i].rule_hdl;
		}
		if (m_filtering.DeleteFilteringHdls(temp_eth_bridge_flt_rule, IPA_IP_v4, IPA_LAN_TO_LAN_MAX_WLAN_CLIENT) == false)
		{
			IPACMERR("Error Deleting Filtering Rule, aborting...\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, IPA_LAN_TO_LAN_MAX_WLAN_CLIENT);

		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
		{
			for(i=0; i<IPA_LAN_TO_LAN_MAX_CPE_CLIENT; i++)
			{
				temp_eth_bridge_flt_rule[i] = lan_client_flt_rule_hdl_v4[i].rule_hdl;
			}
			if (m_filtering.DeleteFilteringHdls(temp_eth_bridge_flt_rule, IPA_IP_v4, IPA_LAN_TO_LAN_MAX_CPE_CLIENT) == false)
			{
				IPACMERR("Error Deleting Filtering Rule, aborting...\n");
				res = IPACM_FAILURE;
				goto fail;
			}
			IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, IPA_LAN_TO_LAN_MAX_CPE_CLIENT);
		}

		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
		{
			for(i=0; i<IPA_LAN_TO_LAN_MAX_USB_CLIENT; i++)
			{
				temp_eth_bridge_flt_rule[i] = lan_client_flt_rule_hdl_v4[i].rule_hdl;
			}
			if (m_filtering.DeleteFilteringHdls(temp_eth_bridge_flt_rule, IPA_IP_v4, IPA_LAN_TO_LAN_MAX_USB_CLIENT) == false)
			{
				IPACMERR("Error Deleting Filtering Rule, aborting...\n");
				res = IPACM_FAILURE;
				goto fail;
			}
			IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, IPA_LAN_TO_LAN_MAX_USB_CLIENT);
		}
#endif
#ifndef FEATURE_ETH_BRIDGE_LE
#ifdef CT_OPT
		if (m_filtering.DeleteFilteringHdls(tcp_ctl_flt_rule_hdl_v4, IPA_IP_v4, NUM_TCP_CTL_FLT_RULE) == false)
		{
			IPACMERR("Error deleting default filtering Rule, aborting...\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, NUM_TCP_CTL_FLT_RULE);
#endif
		for(i=0; i<MAX_OFFLOAD_PAIR; i++)
		{
			if(m_filtering.DeleteFilteringHdls(&(lan2lan_flt_rule_hdl_v4[i].rule_hdl), IPA_IP_v4, 1) == false)
			{
				IPACMERR("Error deleting lan2lan IPv4 flt rules.\n");
				res = IPACM_FAILURE;
				goto fail;
			}
			IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, 1);
		}
		IPACMDBG_H("Deleted lan2lan IPv4 flt rules.\n");
#endif

		/* free private-subnet ipv4 filter rules */
		if (IPACM_Iface::ipacmcfg->ipa_num_private_subnet > IPA_PRIV_SUBNET_FILTER_RULE_HANDLES)
		{
			IPACMERR(" the number of rules are bigger than array, aborting...\n");
			res = IPACM_FAILURE;
			goto fail;
		}

#ifdef FEATURE_IPA_ANDROID
		if(m_filtering.DeleteFilteringHdls(private_fl_rule_hdl, IPA_IP_v4, IPA_MAX_PRIVATE_SUBNET_ENTRIES) == false)
		{
			IPACMERR("Error deleting private subnet IPv4 flt rules.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, IPA_MAX_PRIVATE_SUBNET_ENTRIES);
#else
		if (m_filtering.DeleteFilteringHdls(private_fl_rule_hdl, IPA_IP_v4, IPACM_Iface::ipacmcfg->ipa_num_private_subnet) == false)
		{
			IPACMERR("Error Deleting RuleTable(1) to Filtering, aborting...\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, IPACM_Iface::ipacmcfg->ipa_num_private_subnet);
#endif
	}
	IPACMDBG_H("Finished delete default iface ipv4 filtering rules \n ");

	if (ip_type != IPA_IP_v4 && rx_prop != NULL)
	{
		if(m_filtering.DeleteFilteringHdls(ipv6_icmp_flt_rule_hdl, IPA_IP_v6, NUM_IPV6_ICMP_FLT_RULE) == false)
		{
			IPACMERR("Error Deleting ICMPv6 Filtering Rule, aborting...\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, NUM_IPV6_ICMP_FLT_RULE);

		if (m_filtering.DeleteFilteringHdls(dft_v6fl_rule_hdl, IPA_IP_v6, IPV6_DEFAULT_FILTERTING_RULES) == false)
		{
			IPACMERR("Error Adding RuleTable(1) to Filtering, aborting...\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, IPV6_DEFAULT_FILTERTING_RULES);
#ifdef FEATURE_ETH_BRIDGE_LE
		for(i=0; i<IPA_LAN_TO_LAN_MAX_WLAN_CLIENT; i++)
		{
			temp_eth_bridge_flt_rule[i] = wlan_client_flt_rule_hdl_v6[i].rule_hdl;
		}
		if (m_filtering.DeleteFilteringHdls(temp_eth_bridge_flt_rule, IPA_IP_v6, IPA_LAN_TO_LAN_MAX_WLAN_CLIENT) == false)
		{
			IPACMERR("Error Deleting Filtering Rule, aborting...\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, IPA_LAN_TO_LAN_MAX_WLAN_CLIENT);

		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
		{
			for(i=0; i<IPA_LAN_TO_LAN_MAX_CPE_CLIENT; i++)
			{
				temp_eth_bridge_flt_rule[i] = lan_client_flt_rule_hdl_v6[i].rule_hdl;
			}
			if (m_filtering.DeleteFilteringHdls(temp_eth_bridge_flt_rule, IPA_IP_v6, IPA_LAN_TO_LAN_MAX_CPE_CLIENT) == false)
			{
				IPACMERR("Error Deleting Filtering Rule, aborting...\n");
				res = IPACM_FAILURE;
				goto fail;
			}
			IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, IPA_LAN_TO_LAN_MAX_CPE_CLIENT);
		}

		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
		{
			for(i=0; i<IPA_LAN_TO_LAN_MAX_USB_CLIENT; i++)
			{
				temp_eth_bridge_flt_rule[i] = lan_client_flt_rule_hdl_v6[i].rule_hdl;
			}
			if (m_filtering.DeleteFilteringHdls(temp_eth_bridge_flt_rule, IPA_IP_v6, IPA_LAN_TO_LAN_MAX_USB_CLIENT) == false)
			{
				IPACMERR("Error Deleting Filtering Rule, aborting...\n");
				res = IPACM_FAILURE;
				goto fail;
			}
			IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, IPA_LAN_TO_LAN_MAX_USB_CLIENT);
		}
#endif
#ifndef FEATURE_ETH_BRIDGE_LE
#ifdef CT_OPT
		if (m_filtering.DeleteFilteringHdls(tcp_ctl_flt_rule_hdl_v6, IPA_IP_v6, NUM_TCP_CTL_FLT_RULE) == false)
		{
			IPACMERR("Error deleting default filtering Rule, aborting...\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, NUM_TCP_CTL_FLT_RULE);
#endif
		for(i=0; i<MAX_OFFLOAD_PAIR; i++)
		{
			if(m_filtering.DeleteFilteringHdls(&(lan2lan_flt_rule_hdl_v6[i].rule_hdl), IPA_IP_v6, 1) == false)
			{
				IPACMERR("Error deleting lan2lan IPv4 flt rules.\n");
				res = IPACM_FAILURE;
				goto fail;
			}
			IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, 1);
		}
		IPACMDBG_H("Deleted lan2lan IPv6 flt rules.\n");
#endif
	}
	IPACMDBG_H("Finished delete default iface ipv6 filtering rules \n ");

	if (ip_type != IPA_IP_v6)
	{
		if (m_routing.DeleteRoutingHdl(dft_rt_rule_hdl[0], IPA_IP_v4)
				== false)
		{
			IPACMERR("Routing rule deletion failed!\n");
			res = IPACM_FAILURE;
			goto fail;
		}
	}
	IPACMDBG_H("Finished delete default iface ipv4 rules \n ");

	/* delete default v6 routing rule */
	if (ip_type != IPA_IP_v4)
	{
		/* may have multiple ipv6 iface-RT rules*/
		for (i = 0; i < 2*num_dft_rt_v6; i++)
		{
			if (m_routing.DeleteRoutingHdl(dft_rt_rule_hdl[MAX_DEFAULT_v4_ROUTE_RULES + i], IPA_IP_v6)
					== false)
			{
				IPACMERR("Routing rule deletion failed!\n");
				res = IPACM_FAILURE;
				goto fail;
			}
		}
	}

	IPACMDBG_H("Finished delete default iface ipv6 rules \n ");
	/* clean eth-client header, routing rules */
	IPACMDBG_H("left %d eth clients need to be deleted \n ", num_eth_client);
	for (i = 0; i < num_eth_client; i++)
	{
#ifdef FEATURE_ETH_BRIDGE_LE
			eth_bridge_del_lan_client_rt_rule(get_client_memptr(eth_client, i)->mac, SRC_LAN);
			eth_bridge_del_lan_client_rt_rule(get_client_memptr(eth_client, i)->mac, SRC_WLAN);
			eth_bridge_post_lan_client_event(get_client_memptr(eth_client, i)->mac, IPA_ETH_BRIDGE_LAN_CLIENT_DEL_EVENT);
			eth_bridge_del_lan_client(get_client_memptr(eth_client, i)->mac);
#endif

			/* First reset nat rules and then route rules */
			if(get_client_memptr(eth_client, i)->ipv4_set == true)
			{
				IPACMDBG_H("Clean Nat Rules for ipv4:0x%x\n", get_client_memptr(eth_client, i)->v4_addr);
				CtList->HandleNeighIpAddrDelEvt(get_client_memptr(eth_client, i)->v4_addr);
			}

			if (delete_eth_rtrules(i, IPA_IP_v4))
			{
				IPACMERR("unbale to delete ecm-client v4 route rules for index %d\n", i);
				res = IPACM_FAILURE;
				goto fail;
			}

			if (delete_eth_rtrules(i, IPA_IP_v6))
			{
				IPACMERR("unbale to delete ecm-client v6 route rules for index %d\n", i);
				res = IPACM_FAILURE;
				goto fail;
			}

			IPACMDBG_H("Delete %d client header\n", num_eth_client);


			if(get_client_memptr(eth_client, i)->ipv4_header_set == true)
			{
				if (m_header.DeleteHeaderHdl(get_client_memptr(eth_client, i)->hdr_hdl_v4)
					== false)
				{
					res = IPACM_FAILURE;
					goto fail;
				}
			}

			if(get_client_memptr(eth_client, i)->ipv6_header_set == true)
			{
			if (m_header.DeleteHeaderHdl(get_client_memptr(eth_client, i)->hdr_hdl_v6)
					== false)
			{
				res = IPACM_FAILURE;
				goto fail;
			}
			}
	} /* end of for loop */

	/* free the edm clients cache */
	IPACMDBG_H("Free ecm clients cache\n");

	/* Delete corresponding ipa_rm_resource_name of TX-endpoint after delete all IPV4V6 RT-rule */
	IPACMDBG_H("dev %s delete producer dependency\n", dev_name);
	if (tx_prop != NULL)
	{
		IPACMDBG_H("depend Got pipe %d rm index : %d \n", tx_prop->tx[0].dst_pipe, IPACM_Iface::ipacmcfg->ipa_client_rm_map_tbl[tx_prop->tx[0].dst_pipe]);
		IPACM_Iface::ipacmcfg->DelRmDepend(IPACM_Iface::ipacmcfg->ipa_client_rm_map_tbl[tx_prop->tx[0].dst_pipe]);
	}
	/* check software routing fl rule hdl */
	if (softwarerouting_act == true && rx_prop != NULL)
	{
		handle_software_routing_disable();
	}

	/* posting ip to lan2lan module to delete RT/FILTER rules*/
	post_lan2lan_client_disconnect_msg(IPA_IP_v4);
	post_lan2lan_client_disconnect_msg(IPA_IP_v6);

/* Delete private subnet*/
#ifdef FEATURE_IPA_ANDROID
	if (ip_type != IPA_IP_v6)
	{
		IPACMDBG_H("current IPACM private subnet_addr number(%d)\n", IPACM_Iface::ipacmcfg->ipa_num_private_subnet);
		IPACMDBG_H(" Delete IPACM private subnet_addr as: 0x%x \n", if_ipv4_subnet);
		if(IPACM_Iface::ipacmcfg->DelPrivateSubnet(if_ipv4_subnet, ipa_if_num) == false)
		{
			IPACMERR(" can't Delete IPACM private subnet_addr as: 0x%x \n", if_ipv4_subnet);
		}
	}

	/* reset the IPA-client pipe enum */
	if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat != WAN_IF)
	{
		handle_tethering_client(true, IPACM_CLIENT_USB);
	}
#endif /* defined(FEATURE_IPA_ANDROID)*/
fail:
	if (odu_route_rule_v4_hdl != NULL)
	{
		free(odu_route_rule_v4_hdl);
	}
	if (odu_route_rule_v6_hdl != NULL)
	{
		free(odu_route_rule_v6_hdl);
	}
	/* Delete corresponding ipa_rm_resource_name of RX-endpoint after delete all IPV4V6 FT-rule */
	if (rx_prop != NULL)
	{
		IPACMDBG_H("dev %s add producer dependency\n", dev_name);
		IPACMDBG_H("depend Got pipe %d rm index : %d \n", rx_prop->rx[0].src_pipe, IPACM_Iface::ipacmcfg->ipa_client_rm_map_tbl[rx_prop->rx[0].src_pipe]);
		IPACM_Iface::ipacmcfg->DelRmDepend(IPACM_Iface::ipacmcfg->ipa_client_rm_map_tbl[rx_prop->rx[0].src_pipe]);
		IPACMDBG_H("Finished delete dependency \n ");
		free(rx_prop);
	}

	if (eth_client != NULL)
	{
		free(eth_client);
	}

	if (tx_prop != NULL)
	{
		free(tx_prop);
	}
	if (iface_query != NULL)
	{
		free(iface_query);
	}
#ifdef FEATURE_ETH_BRIDGE_LE
	if(eth_bridge_lan_client_rt_from_lan_info_v4 != NULL)
	{
		free(eth_bridge_lan_client_rt_from_lan_info_v4);
	}
	if(eth_bridge_lan_client_rt_from_lan_info_v6 != NULL)
	{
		free(eth_bridge_lan_client_rt_from_lan_info_v6);
	}
	if(eth_bridge_lan_client_rt_from_wlan_info_v4 != NULL)
	{
		free(eth_bridge_lan_client_rt_from_wlan_info_v4);
	}
	if(eth_bridge_lan_client_rt_from_wlan_info_v6 != NULL)
	{
		free(eth_bridge_lan_client_rt_from_wlan_info_v6);
	}
#endif
	is_active = false;
	post_del_self_evt();

	return res;
}

/* install UL filter rule from Q6 */
int IPACM_Lan::handle_uplink_filter_rule(ipacm_ext_prop *prop, ipa_ip_type iptype, uint8_t xlat_mux_id)
{
	ipa_flt_rule_add flt_rule_entry;
	int len = 0, cnt, ret = IPACM_SUCCESS;
	ipa_ioc_add_flt_rule *pFilteringTable;
	ipa_fltr_installed_notif_req_msg_v01 flt_index;
	int fd;
	int i, index;
	uint32_t value = 0;

	IPACMDBG_H("Set extended property rules in LAN\n");

	if (rx_prop == NULL)
	{
		IPACMDBG_H("No rx properties registered for iface %s\n", dev_name);
		return IPACM_SUCCESS;
	}

	if(prop == NULL || prop->num_ext_props <= 0)
	{
		IPACMDBG_H("No extended property.\n");
		return IPACM_SUCCESS;
	}

	fd = open(IPA_DEVICE_NAME, O_RDWR);
	if (0 == fd)
	{
		IPACMERR("Failed opening %s.\n", IPA_DEVICE_NAME);
		return IPACM_FAILURE;
	}
	if (prop->num_ext_props > MAX_WAN_UL_FILTER_RULES)
	{
		IPACMERR("number of modem UL rules > MAX_WAN_UL_FILTER_RULES, aborting...\n");
		close(fd);
		return IPACM_FAILURE;
	}

	memset(&flt_index, 0, sizeof(flt_index));
	flt_index.source_pipe_index = ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, rx_prop->rx[0].src_pipe);
	flt_index.install_status = IPA_QMI_RESULT_SUCCESS_V01;
#ifndef FEATURE_IPA_V3
	flt_index.filter_index_list_len = prop->num_ext_props;
#else /* defined (FEATURE_IPA_V3) */
	flt_index.rule_id_valid = 1;
	flt_index.rule_id_len = prop->num_ext_props;
#endif
	flt_index.embedded_pipe_index_valid = 1;
	flt_index.embedded_pipe_index = ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, IPA_CLIENT_APPS_LAN_WAN_PROD);
	flt_index.retain_header_valid = 1;
	flt_index.retain_header = 0;
	flt_index.embedded_call_mux_id_valid = 1;
	flt_index.embedded_call_mux_id = IPACM_Iface::ipacmcfg->GetQmapId();
#ifndef FEATURE_IPA_V3
	IPACMDBG_H("flt_index: src pipe: %d, num of rules: %d, ebd pipe: %d, mux id: %d\n",
		flt_index.source_pipe_index, flt_index.filter_index_list_len, flt_index.embedded_pipe_index, flt_index.embedded_call_mux_id);
#else /* defined (FEATURE_IPA_V3) */
	IPACMDBG_H("flt_index: src pipe: %d, num of rules: %d, ebd pipe: %d, mux id: %d\n",
		flt_index.source_pipe_index, flt_index.rule_id_len, flt_index.embedded_pipe_index, flt_index.embedded_call_mux_id);
#endif
	len = sizeof(struct ipa_ioc_add_flt_rule) + prop->num_ext_props * sizeof(struct ipa_flt_rule_add);
	pFilteringTable = (struct ipa_ioc_add_flt_rule*)malloc(len);
	if (pFilteringTable == NULL)
	{
		IPACMERR("Error Locate ipa_flt_rule_add memory...\n");
		return IPACM_FAILURE;
	}
	memset(pFilteringTable, 0, len);

	pFilteringTable->commit = 1;
	pFilteringTable->ep = rx_prop->rx[0].src_pipe;
	pFilteringTable->global = false;
	pFilteringTable->ip = iptype;
	pFilteringTable->num_rules = prop->num_ext_props;

	memset(&flt_rule_entry, 0, sizeof(struct ipa_flt_rule_add)); // Zero All Fields
	flt_rule_entry.at_rear = 1;
#ifdef FEATURE_IPA_V3
	if (flt_rule_entry.rule.eq_attrib.ipv4_frag_eq_present)
		flt_rule_entry.at_rear = 0;
#endif
	flt_rule_entry.flt_rule_hdl = -1;
	flt_rule_entry.status = -1;

	flt_rule_entry.rule.retain_hdr = 0;
	flt_rule_entry.rule.to_uc = 0;
	flt_rule_entry.rule.eq_attrib_type = 1;
	if(iptype == IPA_IP_v4)
	{
		if (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF &&
			IPACM_Wan::isWan_Bridge_Mode())
		{
			IPACMDBG_H("WAN, ODU are in bridge mode \n");
			flt_rule_entry.rule.action = IPA_PASS_TO_ROUTING;
		}
		else
		{
			flt_rule_entry.rule.action = IPA_PASS_TO_SRC_NAT;
		}
	}
	else if(iptype == IPA_IP_v6)
		flt_rule_entry.rule.action = IPA_PASS_TO_ROUTING;
	else
	{
		IPACMERR("IP type is not expected.\n");
		ret = IPACM_FAILURE;
		goto fail;
	}

	index = IPACM_Iface::ipacmcfg->getFltRuleCount(rx_prop->rx[0].src_pipe, iptype);

#ifndef FEATURE_IPA_ANDROID
	if(iptype == IPA_IP_v4 && index != exp_index_v4)
	{
		IPACMDBG_DMESG("### WARNING ### num flt rules for IPv4 on client %d is not expected: %d expected value: %d",
			rx_prop->rx[0].src_pipe, index, exp_index_v4);
	}
	if(iptype == IPA_IP_v6 && index != exp_index_v6)
	{
		IPACMDBG_DMESG("### WARNING ### num flt rules for IPv6 on client %d is not expected: %d expected value: %d",
			rx_prop->rx[0].src_pipe, index, exp_index_v6);
	}
#endif

	for(cnt=0; cnt<prop->num_ext_props; cnt++)
	{
		memcpy(&flt_rule_entry.rule.eq_attrib,
					 &prop->prop[cnt].eq_attrib,
					 sizeof(prop->prop[cnt].eq_attrib));
		flt_rule_entry.rule.rt_tbl_idx = prop->prop[cnt].rt_tbl_idx;

		/* Handle XLAT configuration */
		if ((iptype == IPA_IP_v4) && prop->prop[cnt].is_xlat_rule && (xlat_mux_id != 0))
		{
			/* fill the value of meta-data */
			value = xlat_mux_id;
			flt_rule_entry.rule.eq_attrib.metadata_meq32_present = 1;
			flt_rule_entry.rule.eq_attrib.metadata_meq32.offset = 0;
			flt_rule_entry.rule.eq_attrib.metadata_meq32.value = (value & 0xFF) << 16;
			flt_rule_entry.rule.eq_attrib.metadata_meq32.mask = 0x00FF0000;
			IPACMDBG_H("xlat meta-data is modified for rule: %d has index %d with xlat_mux_id: %d\n",
					cnt, index, xlat_mux_id);
		}
#ifdef FEATURE_IPA_V3
		flt_rule_entry.rule.hashable = prop->prop[cnt].is_rule_hashable;
		flt_rule_entry.rule.rule_id = prop->prop[cnt].rule_id;
#endif
		memcpy(&pFilteringTable->rules[cnt], &flt_rule_entry, sizeof(flt_rule_entry));

		IPACMDBG_H("Modem UL filtering rule %d has index %d\n", cnt, index);
#ifndef FEATURE_IPA_V3
		flt_index.filter_index_list[cnt].filter_index = index;
		flt_index.filter_index_list[cnt].filter_handle = prop->prop[cnt].filter_hdl;
#else /* defined (FEATURE_IPA_V3) */
		flt_index.rule_id[cnt] = prop->prop[cnt].rule_id;
#endif
		index++;
	}

	if(false == m_filtering.SendFilteringRuleIndex(&flt_index))
	{
		IPACMERR("Error sending filtering rule index, aborting...\n");
		ret = IPACM_FAILURE;
		goto fail;
	}

	if(false == m_filtering.AddFilteringRule(pFilteringTable))
	{
		IPACMERR("Error Adding RuleTable to Filtering, aborting...\n");
		ret = IPACM_FAILURE;
		goto fail;
	}
	else
	{
		if(iptype == IPA_IP_v4)
		{
			for(i=0; i<pFilteringTable->num_rules; i++)
			{
				wan_ul_fl_rule_hdl_v4[num_wan_ul_fl_rule_v4] = pFilteringTable->rules[i].flt_rule_hdl;
				num_wan_ul_fl_rule_v4++;
			}
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, iptype, pFilteringTable->num_rules);
		}
		else if(iptype == IPA_IP_v6)
		{
			for(i=0; i<pFilteringTable->num_rules; i++)
			{
				wan_ul_fl_rule_hdl_v6[num_wan_ul_fl_rule_v6] = pFilteringTable->rules[i].flt_rule_hdl;
				num_wan_ul_fl_rule_v6++;
			}
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, iptype, pFilteringTable->num_rules);
		}
		else
		{
			IPACMERR("IP type is not expected.\n");
			goto fail;
		}
	}

fail:
	free(pFilteringTable);
	close(fd);
	return ret;
}

int IPACM_Lan::handle_wan_down_v6(bool is_sta_mode)
{
	ipa_fltr_installed_notif_req_msg_v01 flt_index;
	int fd;

	fd = open(IPA_DEVICE_NAME, O_RDWR);
	if (0 == fd)
	{
		IPACMERR("Failed opening %s.\n", IPA_DEVICE_NAME);
		return IPACM_FAILURE;
	}
	if(m_filtering.DeleteFilteringHdls(ipv6_prefix_flt_rule_hdl, IPA_IP_v6, NUM_IPV6_PREFIX_FLT_RULE) == false)
	{
		close(fd);
		return IPACM_FAILURE;
	}
	IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, NUM_IPV6_PREFIX_FLT_RULE);
	if(is_sta_mode == false)
	{
		if (num_wan_ul_fl_rule_v6 > MAX_WAN_UL_FILTER_RULES)
		{
			IPACMERR(" the number of rules are bigger than array, aborting...\n");
			close(fd);
			return IPACM_FAILURE;
		}
		if (m_filtering.DeleteFilteringHdls(wan_ul_fl_rule_hdl_v6,
			IPA_IP_v6, num_wan_ul_fl_rule_v6) == false)
		{
			IPACMERR("Error Deleting RuleTable(1) to Filtering, aborting...\n");
			close(fd);
			return IPACM_FAILURE;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, num_wan_ul_fl_rule_v6);
		memset(wan_ul_fl_rule_hdl_v6, 0, MAX_WAN_UL_FILTER_RULES * sizeof(uint32_t));
		num_wan_ul_fl_rule_v6 = 0;

		memset(&flt_index, 0, sizeof(flt_index));
		flt_index.source_pipe_index = ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, rx_prop->rx[0].src_pipe);
		flt_index.install_status = IPA_QMI_RESULT_SUCCESS_V01;
#ifndef FEATURE_IPA_V3
		flt_index.filter_index_list_len = 0;
#else /* defined (FEATURE_IPA_V3) */
		flt_index.rule_id_valid = 1;
		flt_index.rule_id_len = 0;
#endif
		flt_index.embedded_pipe_index_valid = 1;
		flt_index.embedded_pipe_index = ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, IPA_CLIENT_APPS_LAN_WAN_PROD);
		flt_index.retain_header_valid = 1;
		flt_index.retain_header = 0;
		flt_index.embedded_call_mux_id_valid = 1;
		flt_index.embedded_call_mux_id = IPACM_Iface::ipacmcfg->GetQmapId();
		if(false == m_filtering.SendFilteringRuleIndex(&flt_index))
		{
			IPACMERR("Error sending filtering rule index, aborting...\n");
			close(fd);
			return IPACM_FAILURE;
		}
	}
	else
	{
		if (m_filtering.DeleteFilteringHdls(&dft_v6fl_rule_hdl[IPV6_DEFAULT_FILTERTING_RULES],
																				IPA_IP_v6, 1) == false)
		{
			IPACMERR("Error Adding RuleTable(1) to Filtering, aborting...\n");
			close(fd);
			return IPACM_FAILURE;
		}
		IPACM_Iface::ipacmcfg->decreaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, 1);
	}
	close(fd);
	return IPACM_SUCCESS;
}


/*handle lan2lan client active*/
int IPACM_Lan::handle_lan2lan_client_active(ipacm_event_data_all *data, ipa_cm_event_id event)
{
	if((tx_prop == NULL) || (rx_prop == NULL))
	{
		IPACMDBG_H("No tx/rx properties registered for iface %s, not posting lan2lan event(%d)\n", dev_name, event);
		return IPACM_SUCCESS;
	}

	if(data == NULL)
	{
		IPACMERR("Event data is empty.\n");
		return IPACM_FAILURE;
	}

	if(data->iptype == IPA_IP_v4 && ip_type != IPA_IP_v4 && ip_type != IPA_IP_MAX)
	{
		IPACMERR("Client has IPv4 addr but iface does not have IPv4 up.\n");
		return IPACM_FAILURE;
	}
	if(data->iptype == IPA_IP_v6 && ip_type != IPA_IP_v6 && ip_type != IPA_IP_MAX)
	{
		IPACMERR("Client has IPv6 addr but iface does not have IPv6 up.\n");
		return IPACM_FAILURE;
	}

	ipacm_cmd_q_data evt_data;
	memset(&evt_data, 0, sizeof(evt_data));

	ipacm_event_lan_client* lan_client;
	lan_client = (ipacm_event_lan_client*)malloc(sizeof(ipacm_event_lan_client));
	if(lan_client == NULL)
	{
		IPACMERR("Unable to allocate memory.\n");
		return IPACM_FAILURE;
	}
	memset(lan_client, 0, sizeof(ipacm_event_lan_client));
	lan_client->iptype = data->iptype;
	lan_client->ipv4_addr = data->ipv4_addr;
	memcpy(lan_client->ipv6_addr, data->ipv6_addr, 4 * sizeof(uint32_t));
	memcpy(lan_client->mac_addr, data->mac_addr, 6 * sizeof(uint8_t));
	lan_client->p_iface = this;

	evt_data.event = event;
	evt_data.evt_data = (void*)lan_client;

	IPACMDBG_H("Posting event: %d\n", event);
	IPACM_EvtDispatcher::PostEvt(&evt_data);
	return IPACM_SUCCESS;
}

int IPACM_Lan::add_lan2lan_flt_rule(ipa_ip_type iptype, uint32_t src_v4_addr, uint32_t dst_v4_addr, uint32_t* src_v6_addr, uint32_t* dst_v6_addr, uint32_t* rule_hdl)
{
	if(rx_prop == NULL)
	{
		IPACMERR("There is no rx_prop for iface %s, not able to add lan2lan filtering rule.\n", dev_name);
		return IPACM_FAILURE;
	}
	if(rule_hdl == NULL)
	{
		IPACMERR("Filteing rule handle is empty.\n");
		return IPACM_FAILURE;
	}

	IPACMDBG_H("Got a new lan2lan flt rule with IP type: %d\n", iptype);

	int i, len, res = IPACM_SUCCESS;
	struct ipa_flt_rule_mdfy flt_rule;
	struct ipa_ioc_mdfy_flt_rule* pFilteringTable;

	len = sizeof(struct ipa_ioc_mdfy_flt_rule) + sizeof(struct ipa_flt_rule_mdfy);

	pFilteringTable = (struct ipa_ioc_mdfy_flt_rule*)malloc(len);

	if (pFilteringTable == NULL)
	{
		IPACMERR("Error allocate lan2lan flt rule memory...\n");
		return IPACM_FAILURE;
	}
	memset(pFilteringTable, 0, len);

	pFilteringTable->commit = 1;
	pFilteringTable->ip = iptype;
	pFilteringTable->num_rules = 1;

	memset(&flt_rule, 0, sizeof(struct ipa_flt_rule_mdfy));

	if(iptype == IPA_IP_v4)
	{
		IPACMDBG_H("src_v4_addr: %d dst_v4_addr: %d\n", src_v4_addr, dst_v4_addr);

		if(num_lan2lan_flt_rule_v4 >= MAX_OFFLOAD_PAIR)
		{
			IPACMERR("Lan2lan flt rule table is full, not able to add.\n");
			res = IPACM_FAILURE;
			goto fail;
		}

		if(false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v4))
		{
			IPACMERR("Failed to get routing table %s handle.\n", IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v4.name);
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACMDBG_H("Routing handle for table %s: %d\n", IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v4.name,
				IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v4.hdl);

		flt_rule.status = -1;
		for(i=0; i<MAX_OFFLOAD_PAIR; i++)
		{
			if(lan2lan_flt_rule_hdl_v4[i].valid == false)
			{
				flt_rule.rule_hdl = lan2lan_flt_rule_hdl_v4[i].rule_hdl;
				break;
			}
		}
		if(i == MAX_OFFLOAD_PAIR)
		{
			IPACMERR("Failed to find a filtering rule.\n");
			res = IPACM_FAILURE;
			goto fail;
		}

		flt_rule.rule.retain_hdr = 0;
		flt_rule.rule.to_uc = 0;
		flt_rule.rule.eq_attrib_type = 0;
		flt_rule.rule.action = IPA_PASS_TO_ROUTING;
		flt_rule.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v4.hdl;

		memcpy(&flt_rule.rule.attrib, &rx_prop->rx[0].attrib,
					 sizeof(flt_rule.rule.attrib));
		IPACMDBG_H("Rx property attrib mask:0x%x\n", rx_prop->rx[0].attrib.attrib_mask);

		flt_rule.rule.attrib.attrib_mask |= IPA_FLT_SRC_ADDR;
		flt_rule.rule.attrib.u.v4.src_addr = src_v4_addr;
		flt_rule.rule.attrib.u.v4.src_addr_mask = 0xFFFFFFFF;

		flt_rule.rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v4.dst_addr = dst_v4_addr;
		flt_rule.rule.attrib.u.v4.dst_addr_mask = 0xFFFFFFFF;

		memcpy(&(pFilteringTable->rules[0]), &flt_rule, sizeof(struct ipa_flt_rule_mdfy));
		if (false == m_filtering.ModifyFilteringRule(pFilteringTable))
		{
			IPACMERR("Error modifying filtering rule.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			lan2lan_flt_rule_hdl_v4[i].valid = true;
			*rule_hdl = lan2lan_flt_rule_hdl_v4[i].rule_hdl;
			num_lan2lan_flt_rule_v4++;
			IPACMDBG_H("Flt rule modified, hdl: 0x%x, status: %d\n", pFilteringTable->rules[0].rule_hdl,
						pFilteringTable->rules[0].status);
		}
	}
	else if(iptype == IPA_IP_v6)
	{
		if(num_lan2lan_flt_rule_v6 >= MAX_OFFLOAD_PAIR)
		{
			IPACMERR("Lan2lan flt rule table is full, not able to add.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		if(src_v6_addr == NULL || dst_v6_addr == NULL)
		{
			IPACMERR("Got IPv6 flt rule but without IPv6 src/dst addr.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACMDBG_H("src_v6_addr: 0x%08x%08x%08x%08x, dst_v6_addr: 0x%08x%08x%08x%08x\n", src_v6_addr[0], src_v6_addr[1],
				src_v6_addr[2], src_v6_addr[3], dst_v6_addr[0], dst_v6_addr[1], dst_v6_addr[2], dst_v6_addr[3]);

		if(false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v6))
		{
			IPACMERR("Failed to get routing table %s handle.\n", IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v6.name);
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACMDBG_H("Routing handle for table %s: %d\n", IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v6.name,
				IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v6.hdl);

		flt_rule.status = -1;
		for(i=0; i<MAX_OFFLOAD_PAIR; i++)
		{
			if(lan2lan_flt_rule_hdl_v6[i].valid == false)
			{
				flt_rule.rule_hdl = lan2lan_flt_rule_hdl_v6[i].rule_hdl;
				break;
			}
		}
		if(i == MAX_OFFLOAD_PAIR)
		{
			IPACMERR("Failed to find a filtering rule handle.\n");
			res = IPACM_FAILURE;
			goto fail;
		}

		flt_rule.rule.retain_hdr = 0;
		flt_rule.rule.to_uc = 0;
		flt_rule.rule.eq_attrib_type = 0;
		flt_rule.rule.action = IPA_PASS_TO_ROUTING;
		flt_rule.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v6.hdl;

		memcpy(&flt_rule.rule.attrib, &rx_prop->rx[0].attrib,
					 sizeof(flt_rule.rule.attrib));
		IPACMDBG_H("Rx property attrib mask:0x%x\n", rx_prop->rx[0].attrib.attrib_mask);

		flt_rule.rule.attrib.attrib_mask |= IPA_FLT_SRC_ADDR;
		flt_rule.rule.attrib.u.v6.src_addr[0] = src_v6_addr[0];
		flt_rule.rule.attrib.u.v6.src_addr[1] = src_v6_addr[1];
		flt_rule.rule.attrib.u.v6.src_addr[2] = src_v6_addr[2];
		flt_rule.rule.attrib.u.v6.src_addr[3] = src_v6_addr[3];
		flt_rule.rule.attrib.u.v6.src_addr_mask[0] = 0xFFFFFFFF;
		flt_rule.rule.attrib.u.v6.src_addr_mask[1] = 0xFFFFFFFF;
		flt_rule.rule.attrib.u.v6.src_addr_mask[2] = 0xFFFFFFFF;
		flt_rule.rule.attrib.u.v6.src_addr_mask[3] = 0xFFFFFFFF;


		flt_rule.rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v6.dst_addr[0] = dst_v6_addr[0];
		flt_rule.rule.attrib.u.v6.dst_addr[1] = dst_v6_addr[1];
		flt_rule.rule.attrib.u.v6.dst_addr[2] = dst_v6_addr[2];
		flt_rule.rule.attrib.u.v6.dst_addr[3] = dst_v6_addr[3];
		flt_rule.rule.attrib.u.v6.dst_addr_mask[0] = 0xFFFFFFFF;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[1] = 0xFFFFFFFF;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[2] = 0xFFFFFFFF;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[3] = 0xFFFFFFFF;

		memcpy(&(pFilteringTable->rules[0]), &flt_rule, sizeof(struct ipa_flt_rule_mdfy));
		if (false == m_filtering.ModifyFilteringRule(pFilteringTable))
		{
			IPACMERR("Error modifying filtering rule.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			lan2lan_flt_rule_hdl_v6[i].valid = true;
			*rule_hdl = lan2lan_flt_rule_hdl_v6[i].rule_hdl;
			num_lan2lan_flt_rule_v6++;
			IPACMDBG_H("Flt rule modified, hdl: 0x%x, status: %d\n", pFilteringTable->rules[0].rule_hdl,
						pFilteringTable->rules[0].status);
		}
	}
	else
	{
		IPACMERR("IP type is not expected.\n");
		res = IPACM_FAILURE;
		goto fail;
	}

fail:
	free(pFilteringTable);
	return res;
}

int IPACM_Lan::add_dummy_lan2lan_flt_rule(ipa_ip_type iptype)
{
	if(rx_prop == NULL)
	{
		IPACMDBG_H("There is no rx_prop for iface %s, not able to add dummy lan2lan filtering rule.\n", dev_name);
		return 0;
	}

	int i, len, res = IPACM_SUCCESS;
	struct ipa_flt_rule_add flt_rule;
	ipa_ioc_add_flt_rule* pFilteringTable;

	len = sizeof(struct ipa_ioc_add_flt_rule) +	MAX_OFFLOAD_PAIR * sizeof(struct ipa_flt_rule_add);

	pFilteringTable = (struct ipa_ioc_add_flt_rule *)malloc(len);
	if (pFilteringTable == NULL)
	{
		IPACMERR("Error allocate flt table memory...\n");
		return IPACM_FAILURE;
	}
	memset(pFilteringTable, 0, len);

	pFilteringTable->commit = 1;
	pFilteringTable->ep = rx_prop->rx[0].src_pipe;
	pFilteringTable->global = false;
	pFilteringTable->ip = iptype;
	pFilteringTable->num_rules = MAX_OFFLOAD_PAIR;

	memset(&flt_rule, 0, sizeof(struct ipa_flt_rule_add));

	flt_rule.rule.retain_hdr = 0;
	flt_rule.at_rear = true;
	flt_rule.flt_rule_hdl = -1;
	flt_rule.status = -1;
	flt_rule.rule.action = IPA_PASS_TO_EXCEPTION;
	memcpy(&flt_rule.rule.attrib, &rx_prop->rx[0].attrib,
			sizeof(flt_rule.rule.attrib));

	if(iptype == IPA_IP_v4)
	{
		flt_rule.rule.attrib.attrib_mask = IPA_FLT_SRC_ADDR | IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v4.src_addr_mask = ~0;
		flt_rule.rule.attrib.u.v4.src_addr = ~0;
		flt_rule.rule.attrib.u.v4.dst_addr_mask = ~0;
		flt_rule.rule.attrib.u.v4.dst_addr = ~0;

		for(i=0; i<MAX_OFFLOAD_PAIR; i++)
		{
			memcpy(&(pFilteringTable->rules[i]), &flt_rule, sizeof(struct ipa_flt_rule_add));
		}

		if (false == m_filtering.AddFilteringRule(pFilteringTable))
		{
			IPACMERR("Error adding dummy lan2lan v4 flt rule\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, MAX_OFFLOAD_PAIR);
			/* copy filter rule hdls */
			for (int i = 0; i < MAX_OFFLOAD_PAIR; i++)
			{
				if (pFilteringTable->rules[i].status == 0)
				{
					lan2lan_flt_rule_hdl_v4[i].rule_hdl = pFilteringTable->rules[i].flt_rule_hdl;
					IPACMDBG_H("Lan2lan v4 flt rule %d hdl:0x%x\n", i, lan2lan_flt_rule_hdl_v4[i].rule_hdl);
				}
				else
				{
					IPACMERR("Failed adding lan2lan v4 flt rule %d\n", i);
					res = IPACM_FAILURE;
					goto fail;
				}
			}
		}
	}
	else if(iptype == IPA_IP_v6)
	{
		flt_rule.rule.attrib.attrib_mask = IPA_FLT_SRC_ADDR | IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v6.src_addr_mask[0] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[1] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[2] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[3] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[0] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[1] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[2] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[3] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[0] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[1] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[2] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[3] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[0] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[1] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[2] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[3] = ~0;

		for(i=0; i<MAX_OFFLOAD_PAIR; i++)
		{
			memcpy(&(pFilteringTable->rules[i]), &flt_rule, sizeof(struct ipa_flt_rule_add));
		}

		if (false == m_filtering.AddFilteringRule(pFilteringTable))
		{
			IPACMERR("Error adding dummy lan2lan v6 flt rule\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, MAX_OFFLOAD_PAIR);
			/* copy filter rule hdls */
			for (int i = 0; i < MAX_OFFLOAD_PAIR; i++)
			{
				if (pFilteringTable->rules[i].status == 0)
				{
					lan2lan_flt_rule_hdl_v6[i].rule_hdl = pFilteringTable->rules[i].flt_rule_hdl;
					IPACMDBG_H("Lan2lan v6 flt rule %d hdl:0x%x\n", i, lan2lan_flt_rule_hdl_v6[i].rule_hdl);
				}
				else
				{
					IPACMERR("Failed adding lan2lan v6 flt rule %d\n", i);
					res = IPACM_FAILURE;
					goto fail;
				}
			}
		}
	}
	else
	{
		IPACMERR("IP type is not expected.\n");
		goto fail;
	}

fail:
	free(pFilteringTable);
	return res;
}

int IPACM_Lan::del_lan2lan_flt_rule(ipa_ip_type iptype, uint32_t rule_hdl)
{
	int i;

	IPACMDBG_H("Del lan2lan flt rule with IP type: %d hdl: %d\n", iptype, rule_hdl);
	if(iptype == IPA_IP_v4)
	{
		for(i=0; i<MAX_OFFLOAD_PAIR; i++)
		{
			if(lan2lan_flt_rule_hdl_v4[i].rule_hdl == rule_hdl)
			{
				if(reset_to_dummy_flt_rule(IPA_IP_v4, rule_hdl) == IPACM_FAILURE)
				{
					IPACMERR("Failed to delete lan2lan v4 flt rule %d\n", rule_hdl);
					return IPACM_FAILURE;
				}
				IPACMDBG_H("Deleted lan2lan v4 flt rule %d\n", rule_hdl);
				lan2lan_flt_rule_hdl_v4[i].valid = false;
				num_lan2lan_flt_rule_v4--;
				break;
			}
		}

		if(i == MAX_OFFLOAD_PAIR) //not finding the rule
		{
			IPACMERR("The rule is not found.\n");
			return IPACM_FAILURE;
		}
	}
	else if(iptype == IPA_IP_v6)
	{
		for(i=0; i<MAX_OFFLOAD_PAIR; i++)
		{
			if(lan2lan_flt_rule_hdl_v6[i].rule_hdl == rule_hdl)
			{
				if(reset_to_dummy_flt_rule(IPA_IP_v6, rule_hdl) == IPACM_FAILURE)
				{
					IPACMERR("Failed to delete lan2lan v6 flt rule %d\n", rule_hdl);
					return IPACM_FAILURE;
				}
				IPACMDBG_H("Deleted lan2lan v6 flt rule %d\n", rule_hdl);
				lan2lan_flt_rule_hdl_v6[i].valid = false;
				num_lan2lan_flt_rule_v6--;
				break;
			}
		}

		if(i == MAX_OFFLOAD_PAIR) //not finding the rule
		{
			IPACMERR("The rule is not found.\n");
			return IPACM_FAILURE;
		}
	}
	else
	{
		IPACMERR("IP type is not expected.\n");
		return IPACM_FAILURE;
	}

	return IPACM_SUCCESS;
}

int IPACM_Lan::reset_to_dummy_flt_rule(ipa_ip_type iptype, uint32_t rule_hdl)
{
	int len, res = IPACM_SUCCESS;
	struct ipa_flt_rule_mdfy flt_rule;
	struct ipa_ioc_mdfy_flt_rule* pFilteringTable;

	IPACMDBG_H("Reset flt rule to dummy, IP type: %d, hdl: %d\n", iptype, rule_hdl);
	len = sizeof(struct ipa_ioc_mdfy_flt_rule) + sizeof(struct ipa_flt_rule_mdfy);
	pFilteringTable = (struct ipa_ioc_mdfy_flt_rule*)malloc(len);

	if (pFilteringTable == NULL)
	{
		IPACMERR("Error allocate flt rule memory...\n");
		return IPACM_FAILURE;
	}
	memset(pFilteringTable, 0, len);

	pFilteringTable->commit = 1;
	pFilteringTable->ip = iptype;
	pFilteringTable->num_rules = 1;

	memset(&flt_rule, 0, sizeof(struct ipa_flt_rule_mdfy));
	flt_rule.status = -1;
	flt_rule.rule_hdl = rule_hdl;

	flt_rule.rule.retain_hdr = 0;
	flt_rule.rule.action = IPA_PASS_TO_EXCEPTION;

	if(iptype == IPA_IP_v4)
	{
		IPACMDBG_H("Reset IPv4 flt rule to dummy\n");

		flt_rule.rule.attrib.attrib_mask = IPA_FLT_SRC_ADDR | IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v4.dst_addr = ~0;
		flt_rule.rule.attrib.u.v4.dst_addr_mask = ~0;
		flt_rule.rule.attrib.u.v4.src_addr = ~0;
		flt_rule.rule.attrib.u.v4.src_addr_mask = ~0;

		memcpy(&(pFilteringTable->rules[0]), &flt_rule, sizeof(struct ipa_flt_rule_mdfy));
		if (false == m_filtering.ModifyFilteringRule(pFilteringTable))
		{
			IPACMERR("Error modifying filtering rule.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			IPACMDBG_H("Flt rule reset to dummy, hdl: 0x%x, status: %d\n", pFilteringTable->rules[0].rule_hdl,
						pFilteringTable->rules[0].status);
		}
	}
	else if(iptype == IPA_IP_v6)
	{
		IPACMDBG_H("Reset IPv6 flt rule to dummy\n");

		flt_rule.rule.attrib.attrib_mask = IPA_FLT_SRC_ADDR | IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v6.src_addr[0] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[1] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[2] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[3] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[0] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[1] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[2] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[3] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[0] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[1] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[2] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[3] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[0] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[1] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[2] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[3] = ~0;


		memcpy(&(pFilteringTable->rules[0]), &flt_rule, sizeof(struct ipa_flt_rule_mdfy));
		if (false == m_filtering.ModifyFilteringRule(pFilteringTable))
		{
			IPACMERR("Error modifying filtering rule.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			IPACMDBG_H("Flt rule reset to dummy, hdl: 0x%x, status: %d\n", pFilteringTable->rules[0].rule_hdl,
						pFilteringTable->rules[0].status);
		}
	}
	else
	{
		IPACMERR("IP type is not expected.\n");
		res = IPACM_FAILURE;
		goto fail;
	}

fail:
	free(pFilteringTable);
	return res;
}

int IPACM_Lan::add_lan2lan_hdr(ipa_ip_type iptype, uint8_t* src_mac, uint8_t* dst_mac, uint32_t* hdr_hdl)
{
	if(tx_prop == NULL)
	{
		IPACMERR("There is no tx_prop, cannot add header.\n");
		return IPACM_FAILURE;
	}
	if(src_mac == NULL || dst_mac == NULL)
	{
		IPACMERR("Either src_mac or dst_mac is null, cannot add header.\n");
		return IPACM_FAILURE;
	}
	if(hdr_hdl == NULL)
	{
		IPACMERR("Header handle is empty.\n");
		return IPACM_FAILURE;
	}

	int i, j, len;
	int res = IPACM_SUCCESS;
	char index[4];
	struct ipa_ioc_copy_hdr sCopyHeader;
	struct ipa_ioc_add_hdr *pHeader;

	IPACMDBG_H("Get lan2lan header request, src_mac: 0x%02x%02x%02x%02x%02x%02x dst_mac: 0x%02x%02x%02x%02x%02x%02x\n",
			src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5], dst_mac[0], dst_mac[1],
			dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5]);

	len = sizeof(struct ipa_ioc_add_hdr) + sizeof(struct ipa_hdr_add);
	pHeader = (struct ipa_ioc_add_hdr *)malloc(len);
	if (pHeader == NULL)
	{
		IPACMERR("Failed to allocate header\n");
		return IPACM_FAILURE;
	}
	memset(pHeader, 0, len);

	if(iptype == IPA_IP_v4)
	{		/* copy partial header for v4*/
		for(i=0; i<tx_prop->num_tx_props; i++)
		{
			if(tx_prop->tx[i].ip == IPA_IP_v4)
			{
				IPACMDBG_H("Got v4-header name from %d tx props\n", i);
				memset(&sCopyHeader, 0, sizeof(sCopyHeader));
				memcpy(sCopyHeader.name, tx_prop->tx[i].hdr_name, sizeof(sCopyHeader.name));

				IPACMDBG_H("Header name: %s\n", sCopyHeader.name);
				if(m_header.CopyHeader(&sCopyHeader) == false)
				{
					IPACMERR("Copy header failed\n");
					res = IPACM_FAILURE;
					goto fail;
				}

				IPACMDBG_H("Header length: %d, paritial: %d\n", sCopyHeader.hdr_len, sCopyHeader.is_partial);
				if (sCopyHeader.hdr_len > IPA_HDR_MAX_SIZE)
				{
					IPACMERR("Header oversize\n");
					res = IPACM_FAILURE;
					goto fail;
				}
				else
				{
					memcpy(pHeader->hdr[0].hdr, sCopyHeader.hdr, sCopyHeader.hdr_len);
				}

				if(sCopyHeader.is_eth2_ofst_valid)
				{
					memcpy(&pHeader->hdr[0].hdr[sCopyHeader.eth2_ofst], dst_mac, IPA_MAC_ADDR_SIZE);
					memcpy(&pHeader->hdr[0].hdr[sCopyHeader.eth2_ofst+IPA_MAC_ADDR_SIZE], src_mac, IPA_MAC_ADDR_SIZE);
				}
				else
				{
					IPACMERR("Ethernet 2 header offset is invalid.\n");
				}

				pHeader->commit = true;
				pHeader->num_hdrs = 1;

				memset(pHeader->hdr[0].name, 0, sizeof(pHeader->hdr[0].name));
				strlcpy(pHeader->hdr[0].name, IPA_LAN_TO_LAN_USB_HDR_NAME_V4, sizeof(pHeader->hdr[0].name));
				pHeader->hdr[0].name[IPA_RESOURCE_NAME_MAX-1] = '\0';
				for(j=0; j<MAX_OFFLOAD_PAIR; j++)
				{
					if( lan2lan_hdr_hdl_v4[j].valid == false)
					{
						IPACMDBG_H("Construct lan2lan hdr with index %d.\n", j);
						break;
					}
				}
				if(j == MAX_OFFLOAD_PAIR)
				{
					IPACMERR("Failed to find an available hdr index.\n");
					res = IPACM_FAILURE;
					goto fail;
				}
				lan2lan_hdr_hdl_v4[j].valid = true;
				snprintf(index,sizeof(index), "%d", j);
				if (strlcat(pHeader->hdr[0].name, index, sizeof(pHeader->hdr[0].name)) > IPA_RESOURCE_NAME_MAX)
				{
					IPACMERR(" header name construction failed exceed length (%d)\n", strlen(pHeader->hdr[0].name));
					res = IPACM_FAILURE;
					goto fail;
				}

				pHeader->hdr[0].hdr_len = sCopyHeader.hdr_len;
				pHeader->hdr[0].is_partial = 0;
				pHeader->hdr[0].hdr_hdl = -1;
				pHeader->hdr[0].status = -1;

				if (m_header.AddHeader(pHeader) == false || pHeader->hdr[0].status != 0)
				{
					IPACMERR("Ioctl IPA_IOC_ADD_HDR failed with status: %d\n", pHeader->hdr[0].status);
					res = IPACM_FAILURE;
					goto fail;
				}
				IPACMDBG_H("Installed v4 full header %s header handle 0x%08x\n", pHeader->hdr[0].name,
							pHeader->hdr[0].hdr_hdl);
				*hdr_hdl = pHeader->hdr[0].hdr_hdl;
				lan2lan_hdr_hdl_v4[j].hdr_hdl = pHeader->hdr[0].hdr_hdl;
				break;
			}
		}
	}
	else if(iptype == IPA_IP_v6)
	{		/* copy partial header for v6*/
		for(i=0; i<tx_prop->num_tx_props; i++)
		{
			if(tx_prop->tx[i].ip == IPA_IP_v6)
			{
				IPACMDBG_H("Got v6-header name from %d tx props\n", i);
				memset(&sCopyHeader, 0, sizeof(sCopyHeader));
				memcpy(sCopyHeader.name, tx_prop->tx[i].hdr_name, sizeof(sCopyHeader.name));

				IPACMDBG_H("Header name: %s\n", sCopyHeader.name);
				if(m_header.CopyHeader(&sCopyHeader) == false)
				{
					IPACMERR("Copy header failed\n");
					res = IPACM_FAILURE;
					goto fail;
				}

				IPACMDBG_H("Header length: %d, paritial: %d\n", sCopyHeader.hdr_len, sCopyHeader.is_partial);
				if (sCopyHeader.hdr_len > IPA_HDR_MAX_SIZE)
				{
					IPACMERR("Header oversize\n");
					res = IPACM_FAILURE;
					goto fail;
				}
				else
				{
					memcpy(pHeader->hdr[0].hdr, sCopyHeader.hdr, sCopyHeader.hdr_len);
				}
				if(sCopyHeader.is_eth2_ofst_valid)
				{
					memcpy(&pHeader->hdr[0].hdr[sCopyHeader.eth2_ofst], dst_mac, IPA_MAC_ADDR_SIZE);
					memcpy(&pHeader->hdr[0].hdr[sCopyHeader.eth2_ofst+IPA_MAC_ADDR_SIZE], src_mac, IPA_MAC_ADDR_SIZE);
				}
				else
				{
					IPACMERR("Ethernet 2 header offset is invalid.\n");
				}

				pHeader->commit = true;
				pHeader->num_hdrs = 1;

				memset(pHeader->hdr[0].name, 0, sizeof(pHeader->hdr[0].name));
				strlcpy(pHeader->hdr[0].name, IPA_LAN_TO_LAN_USB_HDR_NAME_V6, sizeof(pHeader->hdr[0].name));
				pHeader->hdr[0].name[IPA_RESOURCE_NAME_MAX-1] = '\0';
				for(j=0; j<MAX_OFFLOAD_PAIR; j++)
				{
					if( lan2lan_hdr_hdl_v6[j].valid == false)
					{
						IPACMDBG_H("Construct lan2lan hdr with index %d.\n", j);
						break;
					}
				}
				if(j == MAX_OFFLOAD_PAIR)
				{
					IPACMERR("Failed to find an available hdr index.\n");
					res = IPACM_FAILURE;
					goto fail;
				}
				lan2lan_hdr_hdl_v6[j].valid = true;
				snprintf(index,sizeof(index), "%d", j);
				if (strlcat(pHeader->hdr[0].name, index, sizeof(pHeader->hdr[0].name)) > IPA_RESOURCE_NAME_MAX)
				{
					IPACMERR(" header name construction failed exceed length (%d)\n", strlen(pHeader->hdr[0].name));
					res = IPACM_FAILURE;
					goto fail;
				}

				pHeader->hdr[0].hdr_len = sCopyHeader.hdr_len;
				pHeader->hdr[0].is_partial = 0;
				pHeader->hdr[0].hdr_hdl = -1;
				pHeader->hdr[0].status = -1;

				if (m_header.AddHeader(pHeader) == false || pHeader->hdr[0].status != 0)
				{
					IPACMERR("Ioctl IPA_IOC_ADD_HDR failed with status: %d\n", pHeader->hdr[0].status);
					res = IPACM_FAILURE;
					goto fail;
				}
				IPACMDBG_H("Installed v6 full header %s header handle 0x%08x\n", pHeader->hdr[0].name,
							pHeader->hdr[0].hdr_hdl);
				*hdr_hdl = pHeader->hdr[0].hdr_hdl;
				lan2lan_hdr_hdl_v6[j].hdr_hdl = pHeader->hdr[0].hdr_hdl;
				break;
			}
		}
	}
	else
	{
		IPACMERR("IP type is not expected.\n");
	}

fail:
	free(pHeader);
	return res;
}

int IPACM_Lan::del_lan2lan_hdr(ipa_ip_type iptype, uint32_t hdr_hdl)
{
	int i;
	if (m_header.DeleteHeaderHdl(hdr_hdl) == false)
	{
		IPACMERR("Failed to delete header %d\n", hdr_hdl);
		return IPACM_FAILURE;
	}
	IPACMDBG_H("Deleted header %d\n", hdr_hdl);

	if(iptype == IPA_IP_v4)
	{
		for(i=0; i<MAX_OFFLOAD_PAIR; i++)
		{
			if(lan2lan_hdr_hdl_v4[i].hdr_hdl == hdr_hdl)
			{
				lan2lan_hdr_hdl_v4[i].valid = false;
				break;
			}
		}
		if(i == MAX_OFFLOAD_PAIR)
		{
			IPACMERR("Failed to find corresponding hdr hdl.\n");
			return IPACM_FAILURE;
		}
	}
	else if(iptype == IPA_IP_v6)
	{
		for(i=0; i<MAX_OFFLOAD_PAIR; i++)
		{
			if(lan2lan_hdr_hdl_v6[i].hdr_hdl == hdr_hdl)
			{
				lan2lan_hdr_hdl_v6[i].valid = false;
				break;
			}
		}
		if(i == MAX_OFFLOAD_PAIR)
		{
			IPACMERR("Failed to find corresponding hdr hdl.\n");
			return IPACM_FAILURE;
		}
	}
	else
	{
		IPACMERR("IP type is not expected.\n");
		return IPACM_FAILURE;
	}
	return IPACM_SUCCESS;
}

int IPACM_Lan::add_lan2lan_rt_rule(ipa_ip_type iptype, uint32_t src_v4_addr, uint32_t dst_v4_addr,
			uint32_t* src_v6_addr, uint32_t* dst_v6_addr, uint32_t hdr_hdl, lan_to_lan_rt_rule_hdl* rule_hdl)
{
	struct ipa_ioc_add_rt_rule *rt_rule;
	struct ipa_rt_rule_add *rt_rule_entry;
	uint32_t tx_index;
	int len;
	int res = IPACM_SUCCESS;

	IPACMDBG_H("Got a new lan2lan rt rule with IP type: %d\n", iptype);

	if(rule_hdl == NULL)
	{
		IPACMERR("Rule hdl is empty.\n");
		return IPACM_FAILURE;
	}
	memset(rule_hdl, 0, sizeof(lan_to_lan_rt_rule_hdl));

	if(tx_prop == NULL)
	{
		IPACMDBG_H("There is no tx_prop for iface %s, not able to add lan2lan routing rule.\n", dev_name);
		return IPACM_FAILURE;
	}

	len = sizeof(struct ipa_ioc_add_rt_rule) + sizeof(struct ipa_rt_rule_add);
	rt_rule = (struct ipa_ioc_add_rt_rule *)malloc(len);
	if (!rt_rule)
	{
		IPACMERR("Failed to allocate memory for rt rule\n");
		return IPACM_FAILURE;
	}
	memset(rt_rule, 0, len);

	rt_rule->commit = 1;
	rt_rule->num_rules = 1;
	rt_rule->ip = iptype;

	if(iptype == IPA_IP_v4)
	{
		IPACMDBG_H("src_v4_addr: 0x%08x dst_v4_addr: 0x%08x\n", src_v4_addr, dst_v4_addr);

		strcpy(rt_rule->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v4.name);
		rt_rule_entry = &rt_rule->rules[0];
		rt_rule_entry->at_rear = false;
		rt_rule_entry->rt_rule_hdl = 0;
		rt_rule_entry->status = -1;

		for (tx_index = 0; tx_index<iface_query->num_tx_props; tx_index++)
		{
		    if(tx_prop->tx[tx_index].ip != IPA_IP_v4)
		    {
		    	IPACMDBG_H("Tx:%d, iptype: %d conflict ip-type: %d bypass\n",
		    				tx_index, tx_prop->tx[tx_index].ip, IPA_IP_v4);
		    	continue;
		    }

			rt_rule_entry->rule.hdr_hdl = hdr_hdl;
			rt_rule_entry->rule.dst = tx_prop->tx[tx_index].dst_pipe;
			memcpy(&rt_rule_entry->rule.attrib, &tx_prop->tx[tx_index].attrib,
					sizeof(rt_rule_entry->rule.attrib));

			rt_rule_entry->rule.attrib.attrib_mask |= IPA_FLT_SRC_ADDR;
			rt_rule_entry->rule.attrib.u.v4.src_addr      = src_v4_addr;
			rt_rule_entry->rule.attrib.u.v4.src_addr_mask = 0xFFFFFFFF;

			rt_rule_entry->rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
			rt_rule_entry->rule.attrib.u.v4.dst_addr      = dst_v4_addr;
			rt_rule_entry->rule.attrib.u.v4.dst_addr_mask = 0xFFFFFFFF;
			if(m_routing.AddRoutingRule(rt_rule) == false)
			{
				IPACMERR("Routing rule addition failed\n");
				res = IPACM_FAILURE;
				goto fail;
			}
			IPACMDBG_H("Added rt rule hdl: 0x%08x\n", rt_rule_entry->rt_rule_hdl);
			rule_hdl->rule_hdl[rule_hdl->num_rule] = rt_rule_entry->rt_rule_hdl;
			rule_hdl->num_rule++;
		}
	}
	else if(iptype == IPA_IP_v6)
	{
		if(src_v6_addr == NULL || dst_v6_addr == NULL)
		{
			IPACMERR("Got IPv6 rt rule but without IPv6 src/dst addr.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		IPACMDBG_H("src_v6_addr: 0x%08x%08x%08x%08x, dst_v6_addr: 0x%08x%08x%08x%08x\n", src_v6_addr[0], src_v6_addr[1],
				src_v6_addr[2], src_v6_addr[3], dst_v6_addr[0], dst_v6_addr[1], dst_v6_addr[2], dst_v6_addr[3]);

		strcpy(rt_rule->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_lan2lan_v6.name);
		rt_rule_entry = &rt_rule->rules[0];
		rt_rule_entry->at_rear = false;
		rt_rule_entry->rt_rule_hdl = 0;
		rt_rule_entry->status = -1;

		for (tx_index = 0; tx_index<iface_query->num_tx_props; tx_index++)
		{
		    if(tx_prop->tx[tx_index].ip != IPA_IP_v6)
		    {
		    	IPACMDBG_H("Tx:%d, iptype: %d conflict ip-type: %d bypass\n",
		    				tx_index, tx_prop->tx[tx_index].ip, IPA_IP_v6);
		    	continue;
		    }

			rt_rule_entry->rule.hdr_hdl = hdr_hdl;
			rt_rule_entry->rule.dst = tx_prop->tx[tx_index].dst_pipe;
			memcpy(&rt_rule_entry->rule.attrib, &tx_prop->tx[tx_index].attrib,
					sizeof(rt_rule_entry->rule.attrib));

			rt_rule_entry->rule.attrib.attrib_mask |= IPA_FLT_SRC_ADDR;
			rt_rule_entry->rule.attrib.u.v6.src_addr[0] = src_v6_addr[0];
			rt_rule_entry->rule.attrib.u.v6.src_addr[1] = src_v6_addr[1];
			rt_rule_entry->rule.attrib.u.v6.src_addr[2] = src_v6_addr[2];
			rt_rule_entry->rule.attrib.u.v6.src_addr[3] = src_v6_addr[3];
			rt_rule_entry->rule.attrib.u.v6.src_addr_mask[0] = 0xFFFFFFFF;
			rt_rule_entry->rule.attrib.u.v6.src_addr_mask[1] = 0xFFFFFFFF;
			rt_rule_entry->rule.attrib.u.v6.src_addr_mask[2] = 0xFFFFFFFF;
			rt_rule_entry->rule.attrib.u.v6.src_addr_mask[3] = 0xFFFFFFFF;

			rt_rule_entry->rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
			rt_rule_entry->rule.attrib.u.v6.dst_addr[0] = dst_v6_addr[0];
			rt_rule_entry->rule.attrib.u.v6.dst_addr[1] = dst_v6_addr[1];
			rt_rule_entry->rule.attrib.u.v6.dst_addr[2] = dst_v6_addr[2];
			rt_rule_entry->rule.attrib.u.v6.dst_addr[3] = dst_v6_addr[3];
			rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[0] = 0xFFFFFFFF;
			rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[1] = 0xFFFFFFFF;
			rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[2] = 0xFFFFFFFF;
			rt_rule_entry->rule.attrib.u.v6.dst_addr_mask[3] = 0xFFFFFFFF;
			if(m_routing.AddRoutingRule(rt_rule) == false)
			{
				IPACMERR("Routing rule addition failed\n");
				res = IPACM_FAILURE;
				goto fail;
			}
			IPACMDBG_H("Added rt rule hdl: 0x%08x\n", rt_rule_entry->rt_rule_hdl);
			rule_hdl->rule_hdl[rule_hdl->num_rule] = rt_rule_entry->rt_rule_hdl;
			rule_hdl->num_rule++;
		}
	}
	else
	{
		IPACMERR("IP type is not expected.\n");
	}

fail:
	free(rt_rule);
	return res;
}

int IPACM_Lan::del_lan2lan_rt_rule(ipa_ip_type iptype, lan_to_lan_rt_rule_hdl rule_hdl)
{
	if(rule_hdl.num_rule <= 0 || rule_hdl.num_rule > MAX_NUM_PROP)
	{
		IPACMERR("The number of rule handles are not correct.\n");
		return IPACM_FAILURE;
	}

	int i, res = IPACM_SUCCESS;

	IPACMDBG_H("Get %d rule handles with IP type %d\n", rule_hdl.num_rule, iptype);
	for(i=0; i<rule_hdl.num_rule; i++)
	{
		if(m_routing.DeleteRoutingHdl(rule_hdl.rule_hdl[i], iptype) == false)
		{
			IPACMERR("Failed to delete routing rule hdl %d.\n", rule_hdl.rule_hdl[i]);
			res = IPACM_FAILURE;
		}
		IPACMDBG_H("Deleted routing rule handle %d\n",rule_hdl.rule_hdl[i]);
	}
	return res;
}

void IPACM_Lan::post_del_self_evt()
{
	ipacm_cmd_q_data evt;
	ipacm_event_data_fid* fid;
	fid = (ipacm_event_data_fid*)malloc(sizeof(ipacm_event_data_fid));
	if(fid == NULL)
	{
		IPACMERR("Failed to allocate fid memory.\n");
		return;
	}
	memset(fid, 0, sizeof(ipacm_event_data_fid));
	memset(&evt, 0, sizeof(ipacm_cmd_q_data));

	fid->if_index = ipa_if_num;

	evt.evt_data = (void*)fid;
	evt.event = IPA_LAN_DELETE_SELF;

	IPACMDBG_H("Posting event IPA_LAN_DELETE_SELF\n");
	IPACM_EvtDispatcher::PostEvt(&evt);
}

/*handle reset usb-client rt-rules */
int IPACM_Lan::handle_lan_client_reset_rt(ipa_ip_type iptype)
{
	int i, res = IPACM_SUCCESS;

	/* clean eth-client routing rules */
	IPACMDBG_H("left %d eth clients need to be deleted \n ", num_eth_client);
	for (i = 0; i < num_eth_client; i++)
	{
		res = delete_eth_rtrules(i, iptype);
		if (res != IPACM_SUCCESS)
		{
			IPACMERR("Failed to delete old iptype(%d) rules.\n", iptype);
			return res;
		}
	} /* end of for loop */

	/* Pass info to LAN2LAN module */
	res = post_lan2lan_client_disconnect_msg(iptype);
	if (res != IPACM_SUCCESS)
	{
		IPACMERR("Failed to posting delete old iptype(%d) address.\n", iptype);
		return res;
	}
	/* Reset ip-address */
	for (i = 0; i < num_eth_client; i++)
	{
		if(iptype == IPA_IP_v4)
		{
			get_client_memptr(eth_client, i)->ipv4_set = false;
		}
		else
		{
			get_client_memptr(eth_client, i)->ipv6_set = 0;
		}
	} /* end of for loop */
	return res;
}

/*handle lan2lan internal mesg posting*/
int IPACM_Lan::post_lan2lan_client_disconnect_msg(ipa_ip_type iptype)
{
	int i, j;
	ipacm_cmd_q_data evt_data;
	ipacm_event_lan_client* lan_client;

	for (i = 0; i < num_eth_client; i++)
	{
			if((get_client_memptr(eth_client, i)->ipv4_set == true)
				&& (iptype == IPA_IP_v4))
			{
				lan_client = (ipacm_event_lan_client*)malloc(sizeof(ipacm_event_lan_client));
				if(lan_client == NULL)
				{
					IPACMERR("Failed to allocate memory.\n");
					return IPACM_FAILURE;
				}
				memset(lan_client, 0, sizeof(ipacm_event_lan_client));
				lan_client->iptype = IPA_IP_v4;
				lan_client->ipv4_addr = get_client_memptr(eth_client, i)->v4_addr;
				lan_client->p_iface = this;

				memset(&evt_data, 0, sizeof(ipacm_cmd_q_data));
				evt_data.evt_data = (void*)lan_client;
				evt_data.event = IPA_LAN_CLIENT_DISCONNECT;

				IPACMDBG_H("Posting event IPA_LAN_CLIENT_DISCONNECT\n");
				IPACM_EvtDispatcher::PostEvt(&evt_data);
			}

			if((get_client_memptr(eth_client, i)->ipv6_set > 0)
				&& (iptype == IPA_IP_v6))
			{
				for (j = 0; j < get_client_memptr(eth_client, i)->ipv6_set; j++)
				{
					lan_client = (ipacm_event_lan_client*)malloc(sizeof(ipacm_event_lan_client));
					if(lan_client == NULL)
					{
						IPACMERR("Failed to allocate memory.\n");
						return IPACM_FAILURE;
					}
					memset(lan_client, 0, sizeof(ipacm_event_lan_client));
					lan_client->iptype = IPA_IP_v6;
					lan_client->ipv6_addr[0] = get_client_memptr(eth_client, i)->v6_addr[j][0];
					lan_client->ipv6_addr[1] = get_client_memptr(eth_client, i)->v6_addr[j][1];
					lan_client->ipv6_addr[2] = get_client_memptr(eth_client, i)->v6_addr[j][2];
					lan_client->ipv6_addr[3] = get_client_memptr(eth_client, i)->v6_addr[j][3];
					lan_client->p_iface = this;

					memset(&evt_data, 0, sizeof(ipacm_cmd_q_data));
					evt_data.evt_data = (void*)lan_client;
					evt_data.event = IPA_LAN_CLIENT_DISCONNECT;

					IPACMDBG_H("Posting event IPA_LAN_CLIENT_DISCONNECT\n");
					IPACM_EvtDispatcher::PostEvt(&evt_data);
				}
			}
	} /* end of for loop */
	return IPACM_SUCCESS;
}

int IPACM_Lan::install_ipv4_icmp_flt_rule()
{
	int len;
	struct ipa_ioc_add_flt_rule* flt_rule;
	struct ipa_flt_rule_add flt_rule_entry;

	if(rx_prop != NULL)
	{
		len = sizeof(struct ipa_ioc_add_flt_rule) + sizeof(struct ipa_flt_rule_add);

		flt_rule = (struct ipa_ioc_add_flt_rule *)calloc(1, len);
		if (!flt_rule)
		{
			IPACMERR("Error Locate ipa_flt_rule_add memory...\n");
			return IPACM_FAILURE;
		}

		flt_rule->commit = 1;
		flt_rule->ep = rx_prop->rx[0].src_pipe;
		flt_rule->global = false;
		flt_rule->ip = IPA_IP_v4;
		flt_rule->num_rules = 1;

		memset(&flt_rule_entry, 0, sizeof(struct ipa_flt_rule_add));

		flt_rule_entry.rule.retain_hdr = 1;
		flt_rule_entry.rule.to_uc = 0;
		flt_rule_entry.rule.eq_attrib_type = 0;
		flt_rule_entry.at_rear = true;
		flt_rule_entry.flt_rule_hdl = -1;
		flt_rule_entry.status = -1;
		flt_rule_entry.rule.action = IPA_PASS_TO_EXCEPTION;
		memcpy(&flt_rule_entry.rule.attrib, &rx_prop->rx[0].attrib, sizeof(flt_rule_entry.rule.attrib));

		flt_rule_entry.rule.attrib.attrib_mask &= ~((uint32_t)IPA_FLT_META_DATA);
		flt_rule_entry.rule.attrib.attrib_mask = IPA_FLT_PROTOCOL;
		flt_rule_entry.rule.attrib.u.v4.protocol = (uint8_t)IPACM_FIREWALL_IPPROTO_ICMP;
		memcpy(&(flt_rule->rules[0]), &flt_rule_entry, sizeof(struct ipa_flt_rule_add));

		if (m_filtering.AddFilteringRule(flt_rule) == false)
		{
			IPACMERR("Error Adding Filtering rule, aborting...\n");
			free(flt_rule);
			return IPACM_FAILURE;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, 1);
			ipv4_icmp_flt_rule_hdl[0] = flt_rule->rules[0].flt_rule_hdl;
			IPACMDBG_H("IPv4 icmp filter rule HDL:0x%x\n", ipv4_icmp_flt_rule_hdl[0]);
                        free(flt_rule);
		}
	}
	return IPACM_SUCCESS;
}

void IPACM_Lan::install_tcp_ctl_flt_rule(ipa_ip_type iptype)
{
	if (rx_prop == NULL)
	{
		IPACMDBG_H("No rx properties registered for iface %s\n", dev_name);
		return;
	}

	int len, i;
	struct ipa_flt_rule_add flt_rule;
	ipa_ioc_add_flt_rule* pFilteringTable;

	len = sizeof(struct ipa_ioc_add_flt_rule) +	NUM_TCP_CTL_FLT_RULE * sizeof(struct ipa_flt_rule_add);

	pFilteringTable = (struct ipa_ioc_add_flt_rule *)malloc(len);
	if (pFilteringTable == NULL)
	{
		IPACMERR("Error allocate flt table memory...\n");
		return;
	}
	memset(pFilteringTable, 0, len);

	pFilteringTable->commit = 1;
	pFilteringTable->ip = iptype;
	pFilteringTable->ep = rx_prop->rx[0].src_pipe;
	pFilteringTable->global = false;
	pFilteringTable->num_rules = NUM_TCP_CTL_FLT_RULE;

	memset(&flt_rule, 0, sizeof(struct ipa_flt_rule_add));

	flt_rule.at_rear = true;
	flt_rule.flt_rule_hdl = -1;
	flt_rule.status = -1;

	flt_rule.rule.retain_hdr = 1;
	flt_rule.rule.to_uc = 0;
	flt_rule.rule.action = IPA_PASS_TO_EXCEPTION;
#ifdef FEATURE_IPA_V3
	flt_rule.rule.hashable = IPA_RULE_NON_HASHABLE;
#endif
	flt_rule.rule.eq_attrib_type = 1;

	flt_rule.rule.eq_attrib.rule_eq_bitmap = 0;

	if(rx_prop->rx[0].attrib.attrib_mask & IPA_FLT_META_DATA)
	{
		flt_rule.rule.eq_attrib.rule_eq_bitmap |= (1<<14);
		flt_rule.rule.eq_attrib.metadata_meq32_present = 1;
		flt_rule.rule.eq_attrib.metadata_meq32.offset = 0;
		flt_rule.rule.eq_attrib.metadata_meq32.value = rx_prop->rx[0].attrib.meta_data;
		flt_rule.rule.eq_attrib.metadata_meq32.mask = rx_prop->rx[0].attrib.meta_data_mask;
	}

	flt_rule.rule.eq_attrib.rule_eq_bitmap |= (1<<1);
	flt_rule.rule.eq_attrib.protocol_eq_present = 1;
	flt_rule.rule.eq_attrib.protocol_eq = IPACM_FIREWALL_IPPROTO_TCP;

	flt_rule.rule.eq_attrib.rule_eq_bitmap |= (1<<8);
	flt_rule.rule.eq_attrib.num_ihl_offset_meq_32 = 1;
	flt_rule.rule.eq_attrib.ihl_offset_meq_32[0].offset = 12;

	/* add TCP FIN rule*/
	flt_rule.rule.eq_attrib.ihl_offset_meq_32[0].value = (((uint32_t)1)<<TCP_FIN_SHIFT);
	flt_rule.rule.eq_attrib.ihl_offset_meq_32[0].mask = (((uint32_t)1)<<TCP_FIN_SHIFT);
	memcpy(&(pFilteringTable->rules[0]), &flt_rule, sizeof(struct ipa_flt_rule_add));

	/* add TCP SYN rule*/
	flt_rule.rule.eq_attrib.ihl_offset_meq_32[0].value = (((uint32_t)1)<<TCP_SYN_SHIFT);
	flt_rule.rule.eq_attrib.ihl_offset_meq_32[0].mask = (((uint32_t)1)<<TCP_SYN_SHIFT);
	memcpy(&(pFilteringTable->rules[1]), &flt_rule, sizeof(struct ipa_flt_rule_add));

	/* add TCP RST rule*/
	flt_rule.rule.eq_attrib.ihl_offset_meq_32[0].value = (((uint32_t)1)<<TCP_RST_SHIFT);
	flt_rule.rule.eq_attrib.ihl_offset_meq_32[0].mask = (((uint32_t)1)<<TCP_RST_SHIFT);
	memcpy(&(pFilteringTable->rules[2]), &flt_rule, sizeof(struct ipa_flt_rule_add));

	if (false == m_filtering.AddFilteringRule(pFilteringTable))
	{
		IPACMERR("Error adding tcp control flt rule\n");
		goto fail;
	}
	else
	{
		if(iptype == IPA_IP_v4)
		{
			for(i=0; i<NUM_TCP_CTL_FLT_RULE; i++)
			{
				tcp_ctl_flt_rule_hdl_v4[i] = pFilteringTable->rules[i].flt_rule_hdl;
			}
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, iptype, NUM_TCP_CTL_FLT_RULE);
		}
		else
		{
			for(i=0; i<NUM_TCP_CTL_FLT_RULE; i++)
			{
				tcp_ctl_flt_rule_hdl_v6[i] = pFilteringTable->rules[i].flt_rule_hdl;
			}
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, iptype, NUM_TCP_CTL_FLT_RULE);
		}
	}

fail:
	free(pFilteringTable);
	return;
}

int IPACM_Lan::add_dummy_private_subnet_flt_rule(ipa_ip_type iptype)
{
	if(rx_prop == NULL)
	{
		IPACMDBG_H("There is no rx_prop for iface %s, not able to add dummy private subnet filtering rule.\n", dev_name);
		return 0;
	}

	if(iptype == IPA_IP_v6)
	{
		IPACMDBG_H("There is no ipv6 dummy filter rules needed for iface %s\n", dev_name);
		return 0;
	}
	int i, len, res = IPACM_SUCCESS;
	struct ipa_flt_rule_add flt_rule;
	ipa_ioc_add_flt_rule* pFilteringTable;

	len = sizeof(struct ipa_ioc_add_flt_rule) +	IPA_MAX_PRIVATE_SUBNET_ENTRIES * sizeof(struct ipa_flt_rule_add);

	pFilteringTable = (struct ipa_ioc_add_flt_rule *)malloc(len);
	if (pFilteringTable == NULL)
	{
		IPACMERR("Error allocate flt table memory...\n");
		return IPACM_FAILURE;
	}
	memset(pFilteringTable, 0, len);

	pFilteringTable->commit = 1;
	pFilteringTable->ep = rx_prop->rx[0].src_pipe;
	pFilteringTable->global = false;
	pFilteringTable->ip = iptype;
	pFilteringTable->num_rules = IPA_MAX_PRIVATE_SUBNET_ENTRIES;

	memset(&flt_rule, 0, sizeof(struct ipa_flt_rule_add));

	flt_rule.rule.retain_hdr = 0;
	flt_rule.at_rear = true;
	flt_rule.flt_rule_hdl = -1;
	flt_rule.status = -1;
	flt_rule.rule.action = IPA_PASS_TO_EXCEPTION;
	memcpy(&flt_rule.rule.attrib, &rx_prop->rx[0].attrib,
			sizeof(flt_rule.rule.attrib));

	if(iptype == IPA_IP_v4)
	{
		flt_rule.rule.attrib.attrib_mask = IPA_FLT_SRC_ADDR | IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v4.src_addr_mask = ~0;
		flt_rule.rule.attrib.u.v4.src_addr = ~0;
		flt_rule.rule.attrib.u.v4.dst_addr_mask = ~0;
		flt_rule.rule.attrib.u.v4.dst_addr = ~0;

		for(i=0; i<IPA_MAX_PRIVATE_SUBNET_ENTRIES; i++)
		{
			memcpy(&(pFilteringTable->rules[i]), &flt_rule, sizeof(struct ipa_flt_rule_add));
		}

		if (false == m_filtering.AddFilteringRule(pFilteringTable))
		{
			IPACMERR("Error adding dummy private subnet v4 flt rule\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v4, IPA_MAX_PRIVATE_SUBNET_ENTRIES);
			/* copy filter rule hdls */
			for (int i = 0; i < IPA_MAX_PRIVATE_SUBNET_ENTRIES; i++)
			{
				if (pFilteringTable->rules[i].status == 0)
				{
					private_fl_rule_hdl[i] = pFilteringTable->rules[i].flt_rule_hdl;
					IPACMDBG_H("Private subnet v4 flt rule %d hdl:0x%x\n", i, private_fl_rule_hdl[i]);
				}
				else
				{
					IPACMERR("Failed adding lan2lan v4 flt rule %d\n", i);
					res = IPACM_FAILURE;
					goto fail;
				}
			}
		}
	}
fail:
	free(pFilteringTable);
	return res;
}

int IPACM_Lan::handle_private_subnet_android(ipa_ip_type iptype)
{
	int i, len, res = IPACM_SUCCESS;
	struct ipa_flt_rule_mdfy flt_rule;
	struct ipa_ioc_mdfy_flt_rule* pFilteringTable;

	if (rx_prop == NULL)
	{
		IPACMDBG_H("No rx properties registered for iface %s\n", dev_name);
		return IPACM_SUCCESS;
	}

	if(iptype == IPA_IP_v6)
	{
		IPACMDBG_H("There is no ipv6 dummy filter rules needed for iface %s\n", dev_name);
		return 0;
	}
	else
	{
		for(i=0; i<IPA_MAX_PRIVATE_SUBNET_ENTRIES; i++)
		{
			reset_to_dummy_flt_rule(IPA_IP_v4, private_fl_rule_hdl[i]);
		}

		len = sizeof(struct ipa_ioc_mdfy_flt_rule) + (IPACM_Iface::ipacmcfg->ipa_num_private_subnet) * sizeof(struct ipa_flt_rule_mdfy);
		pFilteringTable = (struct ipa_ioc_mdfy_flt_rule*)malloc(len);
		if (!pFilteringTable)
		{
			IPACMERR("Failed to allocate ipa_ioc_mdfy_flt_rule memory...\n");
			return IPACM_FAILURE;
		}
		memset(pFilteringTable, 0, len);

		pFilteringTable->commit = 1;
		pFilteringTable->ip = iptype;
		pFilteringTable->num_rules = (uint8_t)IPACM_Iface::ipacmcfg->ipa_num_private_subnet;

		/* Make LAN-traffic always go A5, use default IPA-RT table */
		if (false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_default_v4))
		{
			IPACMERR("Failed to get routing table handle.\n");
			res = IPACM_FAILURE;
			goto fail;
		}

		memset(&flt_rule, 0, sizeof(struct ipa_flt_rule_mdfy));
		flt_rule.status = -1;

		flt_rule.rule.retain_hdr = 1;
		flt_rule.rule.to_uc = 0;
		flt_rule.rule.action = IPA_PASS_TO_ROUTING;
		flt_rule.rule.eq_attrib_type = 0;
		flt_rule.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_default_v4.hdl;
		IPACMDBG_H("Private filter rule use table: %s\n",IPACM_Iface::ipacmcfg->rt_tbl_default_v4.name);

		memcpy(&flt_rule.rule.attrib, &rx_prop->rx[0].attrib, sizeof(flt_rule.rule.attrib));
		flt_rule.rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;

		for (i = 0; i < (IPACM_Iface::ipacmcfg->ipa_num_private_subnet); i++)
		{
			flt_rule.rule_hdl = private_fl_rule_hdl[i];
			flt_rule.rule.attrib.u.v4.dst_addr_mask = IPACM_Iface::ipacmcfg->private_subnet_table[i].subnet_mask;
			flt_rule.rule.attrib.u.v4.dst_addr = IPACM_Iface::ipacmcfg->private_subnet_table[i].subnet_addr;
			memcpy(&(pFilteringTable->rules[i]), &flt_rule, sizeof(struct ipa_flt_rule_mdfy));
			IPACMDBG_H(" IPACM private subnet_addr as: 0x%x entry(%d)\n", flt_rule.rule.attrib.u.v4.dst_addr, i);
		}

		if (false == m_filtering.ModifyFilteringRule(pFilteringTable))
		{
			IPACMERR("Failed to modify private subnet filtering rules.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
	}
fail:
	if(pFilteringTable != NULL)
	{
		free(pFilteringTable);
	}
	return res;
}

int IPACM_Lan::install_ipv6_prefix_flt_rule(uint32_t* prefix)
{
	if(prefix == NULL)
	{
		IPACMERR("IPv6 prefix is empty.\n");
		return IPACM_FAILURE;
	}
	IPACMDBG_H("Receive IPv6 prefix: 0x%08x%08x.\n", prefix[0], prefix[1]);

	int len;
	struct ipa_ioc_add_flt_rule* flt_rule;
	struct ipa_flt_rule_add flt_rule_entry;

	if(rx_prop != NULL)
	{
		len = sizeof(struct ipa_ioc_add_flt_rule) + sizeof(struct ipa_flt_rule_add);

		flt_rule = (struct ipa_ioc_add_flt_rule *)calloc(1, len);
		if (!flt_rule)
		{
			IPACMERR("Error Locate ipa_flt_rule_add memory...\n");
			return IPACM_FAILURE;
		}

		flt_rule->commit = 1;
		flt_rule->ep = rx_prop->rx[0].src_pipe;
		flt_rule->global = false;
		flt_rule->ip = IPA_IP_v6;
		flt_rule->num_rules = 1;

		memset(&flt_rule_entry, 0, sizeof(struct ipa_flt_rule_add));

		flt_rule_entry.rule.retain_hdr = 1;
		flt_rule_entry.rule.to_uc = 0;
		flt_rule_entry.rule.eq_attrib_type = 0;
		flt_rule_entry.at_rear = true;
		flt_rule_entry.flt_rule_hdl = -1;
		flt_rule_entry.status = -1;
		flt_rule_entry.rule.action = IPA_PASS_TO_EXCEPTION;
#ifdef FEATURE_IPA_V3
		flt_rule_entry.rule.hashable = IPA_RULE_NON_HASHABLE;
#endif
		memcpy(&flt_rule_entry.rule.attrib, &rx_prop->rx[0].attrib, sizeof(flt_rule_entry.rule.attrib));
		flt_rule_entry.rule.attrib.attrib_mask = flt_rule_entry.rule.attrib.attrib_mask & ~((uint32_t)IPA_FLT_META_DATA);
		flt_rule_entry.rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
		flt_rule_entry.rule.attrib.u.v6.dst_addr[0] = prefix[0];
		flt_rule_entry.rule.attrib.u.v6.dst_addr[1] = prefix[1];
		flt_rule_entry.rule.attrib.u.v6.dst_addr[2] = 0x0;
		flt_rule_entry.rule.attrib.u.v6.dst_addr[3] = 0x0;
		flt_rule_entry.rule.attrib.u.v6.dst_addr_mask[0] = 0xFFFFFFFF;
		flt_rule_entry.rule.attrib.u.v6.dst_addr_mask[1] = 0xFFFFFFFF;
		flt_rule_entry.rule.attrib.u.v6.dst_addr_mask[2] = 0x0;
		flt_rule_entry.rule.attrib.u.v6.dst_addr_mask[3] = 0x0;
		memcpy(&(flt_rule->rules[0]), &flt_rule_entry, sizeof(struct ipa_flt_rule_add));

		if (m_filtering.AddFilteringRule(flt_rule) == false)
		{
			IPACMERR("Error Adding Filtering rule, aborting...\n");
			free(flt_rule);
			return IPACM_FAILURE;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, 1);
			ipv6_prefix_flt_rule_hdl[0] = flt_rule->rules[0].flt_rule_hdl;
			IPACMDBG_H("IPv6 prefix filter rule HDL:0x%x\n", ipv6_prefix_flt_rule_hdl[0]);
			free(flt_rule);
		}
	}
	return IPACM_SUCCESS;
}

int IPACM_Lan::install_ipv6_icmp_flt_rule()
{

	int len;
	struct ipa_ioc_add_flt_rule* flt_rule;
	struct ipa_flt_rule_add flt_rule_entry;

	if(rx_prop != NULL)
	{
		len = sizeof(struct ipa_ioc_add_flt_rule) + sizeof(struct ipa_flt_rule_add);

		flt_rule = (struct ipa_ioc_add_flt_rule *)calloc(1, len);
		if (!flt_rule)
		{
			IPACMERR("Error Locate ipa_flt_rule_add memory...\n");
			return IPACM_FAILURE;
		}

		flt_rule->commit = 1;
		flt_rule->ep = rx_prop->rx[0].src_pipe;
		flt_rule->global = false;
		flt_rule->ip = IPA_IP_v6;
		flt_rule->num_rules = 1;

		memset(&flt_rule_entry, 0, sizeof(struct ipa_flt_rule_add));

		flt_rule_entry.rule.retain_hdr = 1;
		flt_rule_entry.rule.to_uc = 0;
		flt_rule_entry.rule.eq_attrib_type = 0;
		flt_rule_entry.at_rear = true;
		flt_rule_entry.flt_rule_hdl = -1;
		flt_rule_entry.status = -1;
		flt_rule_entry.rule.action = IPA_PASS_TO_EXCEPTION;
#ifdef FEATURE_IPA_V3
		flt_rule_entry.rule.hashable = IPA_RULE_NON_HASHABLE;
#endif
		memcpy(&flt_rule_entry.rule.attrib, &rx_prop->rx[0].attrib, sizeof(flt_rule_entry.rule.attrib));
		flt_rule_entry.rule.attrib.attrib_mask = flt_rule_entry.rule.attrib.attrib_mask & ~((uint32_t)IPA_FLT_META_DATA);
		flt_rule_entry.rule.attrib.attrib_mask |= IPA_FLT_NEXT_HDR;
		flt_rule_entry.rule.attrib.u.v6.next_hdr = (uint8_t)IPACM_FIREWALL_IPPROTO_ICMP6;
		memcpy(&(flt_rule->rules[0]), &flt_rule_entry, sizeof(struct ipa_flt_rule_add));

		if (m_filtering.AddFilteringRule(flt_rule) == false)
		{
			IPACMERR("Error Adding Filtering rule, aborting...\n");
			free(flt_rule);
			return IPACM_FAILURE;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, IPA_IP_v6, 1);
			ipv6_icmp_flt_rule_hdl[0] = flt_rule->rules[0].flt_rule_hdl;
			IPACMDBG_H("IPv6 icmp filter rule HDL:0x%x\n", ipv6_icmp_flt_rule_hdl[0]);
			free(flt_rule);
		}
	}
	return IPACM_SUCCESS;
}

int IPACM_Lan::handle_addr_evt_odu_bridge(ipacm_event_data_addr* data)
{
	int fd, res = IPACM_SUCCESS;
	struct in6_addr ipv6_addr;
	if(data == NULL)
	{
		IPACMERR("Failed to get interface IP address.\n");
		return IPACM_FAILURE;
	}

	if(data->iptype == IPA_IP_v6)
	{
		fd = open(IPACM_Iface::ipacmcfg->DEVICE_NAME_ODU, O_RDWR);
		if(fd == 0)
		{
			IPACMERR("Failed to open %s.\n", IPACM_Iface::ipacmcfg->DEVICE_NAME_ODU);
			return IPACM_FAILURE;
		}

		memcpy(&ipv6_addr, data->ipv6_addr, sizeof(struct in6_addr));

		if( ioctl(fd, ODU_BRIDGE_IOC_SET_LLV6_ADDR, &ipv6_addr) )
		{
			IPACMERR("Failed to write IPv6 address to odu driver.\n");
			res = IPACM_FAILURE;
		}
		num_dft_rt_v6++;
		close(fd);
	}

	return res;
}

int IPACM_Lan::eth_bridge_handle_dummy_wlan_client_flt_rule(ipa_ip_type iptype)
{
	if(rx_prop == NULL)
	{
		IPACMDBG_H("There is no rx_prop for iface %s, not able to add dummy wlan client specific filtering rule.\n", dev_name);
		return 0;
	}

	int i, len, res = IPACM_SUCCESS;
	struct ipa_flt_rule_add flt_rule;
	ipa_ioc_add_flt_rule* pFilteringTable;

	len = sizeof(struct ipa_ioc_add_flt_rule) +	IPA_LAN_TO_LAN_MAX_WLAN_CLIENT * sizeof(struct ipa_flt_rule_add);

	pFilteringTable = (struct ipa_ioc_add_flt_rule *)malloc(len);
	if (pFilteringTable == NULL)
	{
		IPACMERR("Error allocate flt table memory...\n");
		return IPACM_FAILURE;
	}
	memset(pFilteringTable, 0, len);

	pFilteringTable->commit = 1;
	pFilteringTable->ep = rx_prop->rx[0].src_pipe;
	pFilteringTable->global = false;
	pFilteringTable->ip = iptype;
	pFilteringTable->num_rules = IPA_LAN_TO_LAN_MAX_WLAN_CLIENT;

	memset(&flt_rule, 0, sizeof(struct ipa_flt_rule_add));

	flt_rule.rule.retain_hdr = 0;
	flt_rule.at_rear = true;
	flt_rule.flt_rule_hdl = -1;
	flt_rule.status = -1;
	flt_rule.rule.action = IPA_PASS_TO_EXCEPTION;
	memcpy(&flt_rule.rule.attrib, &rx_prop->rx[0].attrib,
			sizeof(flt_rule.rule.attrib));

	if(iptype == IPA_IP_v4)
	{
		flt_rule.rule.attrib.attrib_mask = IPA_FLT_SRC_ADDR | IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v4.src_addr_mask = ~0;
		flt_rule.rule.attrib.u.v4.src_addr = ~0;
		flt_rule.rule.attrib.u.v4.dst_addr_mask = ~0;
		flt_rule.rule.attrib.u.v4.dst_addr = ~0;

		for(i=0; i<IPA_LAN_TO_LAN_MAX_WLAN_CLIENT; i++)
		{
			memcpy(&(pFilteringTable->rules[i]), &flt_rule, sizeof(struct ipa_flt_rule_add));
		}

		if (false == m_filtering.AddFilteringRule(pFilteringTable))
		{
			IPACMERR("Error adding dummy lan2lan v4 flt rule\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, iptype, IPA_LAN_TO_LAN_MAX_WLAN_CLIENT);
			/* copy filter rule hdls */
			for (int i = 0; i < IPA_LAN_TO_LAN_MAX_WLAN_CLIENT; i++)
			{
				if (pFilteringTable->rules[i].status == 0)
				{
					wlan_client_flt_rule_hdl_v4[i].rule_hdl = pFilteringTable->rules[i].flt_rule_hdl;
					wlan_client_flt_rule_hdl_v4[i].valid = true;
					IPACMDBG_H("Wlan client v4 flt rule %d hdl:0x%x\n", i, wlan_client_flt_rule_hdl_v4[i].rule_hdl);
				}
				else
				{
					IPACMERR("Failed adding wlan client v4 flt rule %d\n", i);
					res = IPACM_FAILURE;
					goto fail;
				}
			}
		}
	}
	else if(iptype == IPA_IP_v6)
	{
		flt_rule.rule.attrib.attrib_mask = IPA_FLT_SRC_ADDR | IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v6.src_addr_mask[0] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[1] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[2] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[3] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[0] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[1] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[2] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[3] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[0] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[1] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[2] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[3] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[0] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[1] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[2] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[3] = ~0;

		for(i=0; i<IPA_LAN_TO_LAN_MAX_WLAN_CLIENT; i++)
		{
			memcpy(&(pFilteringTable->rules[i]), &flt_rule, sizeof(struct ipa_flt_rule_add));
		}

		if (false == m_filtering.AddFilteringRule(pFilteringTable))
		{
			IPACMERR("Error adding dummy lan2lan v6 flt rule\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, iptype, IPA_LAN_TO_LAN_MAX_WLAN_CLIENT);
			/* copy filter rule hdls */
			for (int i = 0; i < IPA_LAN_TO_LAN_MAX_WLAN_CLIENT; i++)
			{
				if (pFilteringTable->rules[i].status == 0)
				{
					wlan_client_flt_rule_hdl_v6[i].rule_hdl = pFilteringTable->rules[i].flt_rule_hdl;
					wlan_client_flt_rule_hdl_v6[i].valid = true;
					IPACMDBG_H("Wlan client v6 flt rule %d hdl:0x%x\n", i, wlan_client_flt_rule_hdl_v6[i].rule_hdl);
				}
				else
				{
					IPACMERR("Failed adding wlan client v6 flt rule %d\n", i);
					res = IPACM_FAILURE;
					goto fail;
				}
			}
		}
	}
	else
	{
		IPACMERR("IP type is not expected.\n");
		goto fail;
	}

fail:
	free(pFilteringTable);
	return res;
}

int IPACM_Lan::eth_bridge_handle_dummy_lan_client_flt_rule(ipa_ip_type iptype)
{
	if(rx_prop == NULL)
	{
		IPACMDBG_H("There is no rx_prop for iface %s, not able to add dummy lan client specific filtering rule.\n", dev_name);
		return 0;
	}

	int i, len, res = IPACM_SUCCESS, num_dummy_rules;
	struct ipa_flt_rule_add flt_rule;
	ipa_ioc_add_flt_rule* pFilteringTable;

	if (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
	{
		num_dummy_rules = IPA_LAN_TO_LAN_MAX_CPE_CLIENT;
	}
	if (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
	{
		num_dummy_rules = IPA_LAN_TO_LAN_MAX_USB_CLIENT;
	}

	len = sizeof(struct ipa_ioc_add_flt_rule) + num_dummy_rules * sizeof(struct ipa_flt_rule_add);

	pFilteringTable = (struct ipa_ioc_add_flt_rule *)malloc(len);
	if (pFilteringTable == NULL)
	{
		IPACMERR("Error allocate flt table memory...\n");
		return IPACM_FAILURE;
	}
	memset(pFilteringTable, 0, len);

	pFilteringTable->commit = 1;
	pFilteringTable->ep = rx_prop->rx[0].src_pipe;
	pFilteringTable->global = false;
	pFilteringTable->ip = iptype;
	pFilteringTable->num_rules = num_dummy_rules;

	memset(&flt_rule, 0, sizeof(struct ipa_flt_rule_add));

	flt_rule.rule.retain_hdr = 0;
	flt_rule.at_rear = true;
	flt_rule.flt_rule_hdl = -1;
	flt_rule.status = -1;
	flt_rule.rule.action = IPA_PASS_TO_EXCEPTION;
	memcpy(&flt_rule.rule.attrib, &rx_prop->rx[0].attrib,
			sizeof(flt_rule.rule.attrib));

	if(iptype == IPA_IP_v4)
	{
		flt_rule.rule.attrib.attrib_mask = IPA_FLT_SRC_ADDR | IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v4.src_addr_mask = ~0;
		flt_rule.rule.attrib.u.v4.src_addr = ~0;
		flt_rule.rule.attrib.u.v4.dst_addr_mask = ~0;
		flt_rule.rule.attrib.u.v4.dst_addr = ~0;

		for(i=0; i < num_dummy_rules; i++)
		{
			memcpy(&(pFilteringTable->rules[i]), &flt_rule, sizeof(struct ipa_flt_rule_add));
		}

		if (false == m_filtering.AddFilteringRule(pFilteringTable))
		{
			IPACMERR("Error adding dummy lan2lan v4 flt rule\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, iptype, num_dummy_rules);
			/* copy filter rule hdls */
			for (int i = 0; i < num_dummy_rules; i++)
			{
				if (pFilteringTable->rules[i].status == 0)
				{
					lan_client_flt_rule_hdl_v4[i].rule_hdl = pFilteringTable->rules[i].flt_rule_hdl;
					lan_client_flt_rule_hdl_v4[i].valid = true;
					IPACMDBG_H("Lan client v4 flt rule %d hdl:0x%x\n", i, lan_client_flt_rule_hdl_v4[i].rule_hdl);
				}
				else
				{
					IPACMERR("Failed adding lan client v4 flt rule %d\n", i);
					res = IPACM_FAILURE;
					goto fail;
				}
			}
		}
	}
	else if(iptype == IPA_IP_v6)
	{
		flt_rule.rule.attrib.attrib_mask = IPA_FLT_SRC_ADDR | IPA_FLT_DST_ADDR;
		flt_rule.rule.attrib.u.v6.src_addr_mask[0] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[1] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[2] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr_mask[3] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[0] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[1] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[2] = ~0;
		flt_rule.rule.attrib.u.v6.src_addr[3] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[0] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[1] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[2] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr_mask[3] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[0] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[1] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[2] = ~0;
		flt_rule.rule.attrib.u.v6.dst_addr[3] = ~0;

		for(i=0; i<num_dummy_rules; i++)
		{
			memcpy(&(pFilteringTable->rules[i]), &flt_rule, sizeof(struct ipa_flt_rule_add));
		}

		if (false == m_filtering.AddFilteringRule(pFilteringTable))
		{
			IPACMERR("Error adding dummy lan2lan v6 flt rule\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		else
		{
			IPACM_Iface::ipacmcfg->increaseFltRuleCount(rx_prop->rx[0].src_pipe, iptype, num_dummy_rules);
			/* copy filter rule hdls */
			for (int i = 0; i < num_dummy_rules; i++)
			{
				if (pFilteringTable->rules[i].status == 0)
				{
					lan_client_flt_rule_hdl_v6[i].rule_hdl = pFilteringTable->rules[i].flt_rule_hdl;
					lan_client_flt_rule_hdl_v6[i].valid = true;
					IPACMDBG_H("Lan client v6 flt rule %d hdl:0x%x\n", i, lan_client_flt_rule_hdl_v6[i].rule_hdl);
				}
				else
				{
					IPACMERR("Failed adding lan client v6 flt rule %d\n", i);
					res = IPACM_FAILURE;
					goto fail;
				}
			}
		}
	}
	else
	{
		IPACMERR("IP type is not expected.\n");
		goto fail;
	}

fail:
	free(pFilteringTable);
	return res;
}

int IPACM_Lan::eth_bridge_post_lan_client_event(uint8_t* mac_addr, ipa_cm_event_id evt)
{
	if(mac_addr == NULL)
	{
		IPACMERR("Event mac is empty.\n");
		return IPACM_FAILURE;
	}

	ipacm_cmd_q_data evt_data;
	memset(&evt_data, 0, sizeof(evt_data));

	ipacm_event_data_mac* mac;
	mac = (ipacm_event_data_mac*)malloc(sizeof(ipacm_event_data_mac));
	if(mac == NULL)
	{
		IPACMERR("Unable to allocate memory.\n");
		return IPACM_FAILURE;
	}
	memset(mac, 0, sizeof(ipacm_event_data_mac));
	memcpy(mac->mac_addr, mac_addr, 6 * sizeof(uint8_t));
	mac->if_index = ipa_if_num;

	evt_data.event = evt;
	evt_data.evt_data = (void*)mac;
	IPACMDBG_H("Posting event: %d if_index: %d \n", evt, mac->if_index);
	IPACM_EvtDispatcher::PostEvt(&evt_data);
	return IPACM_SUCCESS;
}

int IPACM_Lan::eth_bridge_add_wlan_client_flt_rule(uint8_t* mac, ipa_ip_type iptype)
{
	int i, len, res = IPACM_SUCCESS, client_position;
	struct ipa_flt_rule_mdfy flt_rule;
	struct ipa_ioc_mdfy_flt_rule* pFilteringTable = NULL;
	bool client_is_found = false;

	if (rx_prop == NULL)
	{
		IPACMDBG_H("No rx properties registered for iface %s\n", dev_name);
		return IPACM_FAILURE;
	}
	if(mac == NULL)
	{
		IPACMERR("MAC address is empty.\n");
		return IPACM_FAILURE;
	}
	if(IPACM_Lan::lan_to_wlan_hdr_proc_ctx.valid == false)
	{
		IPACMDBG_H("USB to WLAN hdr proc ctx has not been set, don't add client specific flt rule.\n");
		return IPACM_FAILURE;
	}

	IPACMDBG_H("Received WLAN client MAC 0x%02x%02x%02x%02x%02x%02x.\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	for(i=0; i<wlan_client_flt_info_count; i++)
	{
		if(memcmp(eth_bridge_wlan_client_flt_info[i].mac, mac, sizeof(eth_bridge_wlan_client_flt_info[i].mac)) == 0)
		{
			client_is_found = true;
			client_position = i;
			if( (iptype == IPA_IP_v4 && eth_bridge_wlan_client_flt_info[i].flt_rule_set_v4 == true)
				|| (iptype == IPA_IP_v6 && eth_bridge_wlan_client_flt_info[i].flt_rule_set_v6 == true))
			{
				IPACMDBG_H("Flt rule for iptype %d has been set.\n", iptype);
				return IPACM_SUCCESS;
			}
			break;
		}
	}

	if(client_is_found == false && wlan_client_flt_info_count == IPA_LAN_TO_LAN_MAX_WLAN_CLIENT)
	{
		IPACMDBG_H("The wlan client flt table is already full.\n");
		return IPACM_FAILURE;
	}

	len = sizeof(struct ipa_ioc_mdfy_flt_rule) + sizeof(struct ipa_flt_rule_mdfy);
	pFilteringTable = (struct ipa_ioc_mdfy_flt_rule*)malloc(len);
	if (!pFilteringTable)
	{
		IPACMERR("Failed to allocate ipa_ioc_mdfy_flt_rule memory...\n");
		return IPACM_FAILURE;
	}
	memset(pFilteringTable, 0, len);

	/* add mac based rule*/
	pFilteringTable->commit = 1;
	pFilteringTable->ip = iptype;
	pFilteringTable->num_rules = 1;

	memset(&flt_rule, 0, sizeof(struct ipa_flt_rule_mdfy));
	flt_rule.status = -1;

	flt_rule.rule.retain_hdr = 0;
	flt_rule.rule.to_uc = 0;
	flt_rule.rule.action = IPA_PASS_TO_ROUTING;
	flt_rule.rule.eq_attrib_type = 0;

	/* point to USB-WLAN routing table */
	if(iptype == IPA_IP_v4)
	{
		if (false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_wlan_v4))
		{
			IPACMERR("Failed to get routing table handle.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		flt_rule.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_wlan_v4.hdl;
		IPACMDBG_H("LAN->WLAN filter rule use table: %s\n",IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_wlan_v4.name);
	}
	else
	{
		if (false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_wlan_v6))
		{
			IPACMERR("Failed to get routing table handle.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		flt_rule.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_wlan_v6.hdl;
		IPACMDBG_H("LAN->WLAN filter rule use table: %s\n",IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_wlan_v6.name);
	}

	memcpy(&flt_rule.rule.attrib, &rx_prop->rx[0].attrib, sizeof(flt_rule.rule.attrib));
	if(IPACM_Lan::lan_hdr_type == IPA_HDR_L2_ETHERNET_II)
	{
		flt_rule.rule.attrib.attrib_mask |= IPA_FLT_MAC_DST_ADDR_ETHER_II;
	}
	else if(IPACM_Lan::lan_hdr_type == IPA_HDR_L2_802_3)
	{
		flt_rule.rule.attrib.attrib_mask |= IPA_FLT_MAC_DST_ADDR_802_3;
	}
	else
	{
		IPACMERR("USB hdr type is not expected.\n");
		res = IPACM_FAILURE;
		goto fail;
	}
	memcpy(flt_rule.rule.attrib.dst_mac_addr, mac, sizeof(flt_rule.rule.attrib.dst_mac_addr));
	memset(flt_rule.rule.attrib.dst_mac_addr_mask, 0xFF, sizeof(flt_rule.rule.attrib.dst_mac_addr_mask));

	if(iptype == IPA_IP_v4)
	{
		for(i=0; i<IPA_LAN_TO_LAN_MAX_WLAN_CLIENT; i++)
		{
			if(wlan_client_flt_rule_hdl_v4[i].valid == true)
			{
				flt_rule.rule_hdl = wlan_client_flt_rule_hdl_v4[i].rule_hdl;
				wlan_client_flt_rule_hdl_v4[i].valid = false;
				break;
			}
		}
		if(i == IPA_LAN_TO_LAN_MAX_WLAN_CLIENT)
		{
			IPACMDBG_H("Cannot find a valid flt rule hdl.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
	}
	else
	{
		for(i=0; i<IPA_LAN_TO_LAN_MAX_WLAN_CLIENT; i++)
		{
			if(wlan_client_flt_rule_hdl_v6[i].valid == true)
			{
				flt_rule.rule_hdl = wlan_client_flt_rule_hdl_v6[i].rule_hdl;
				wlan_client_flt_rule_hdl_v6[i].valid = false;
				break;
			}
		}
		if(i == IPA_LAN_TO_LAN_MAX_WLAN_CLIENT)
		{
			IPACMDBG_H("Cannot find a valid flt rule hdl.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
	}

	memcpy(&(pFilteringTable->rules[0]), &flt_rule, sizeof(struct ipa_flt_rule_mdfy));
	if (false == m_filtering.ModifyFilteringRule(pFilteringTable))
	{
		IPACMERR("Failed to add wlan client filtering rules.\n");
		res = IPACM_FAILURE;
		goto fail;
	}

	if(client_is_found == false)
	{
		client_position = wlan_client_flt_info_count;
		wlan_client_flt_info_count++;
	}

	memcpy(eth_bridge_wlan_client_flt_info[client_position].mac, mac, sizeof(eth_bridge_wlan_client_flt_info[client_position].mac));
	if(iptype == IPA_IP_v4)
	{
		eth_bridge_wlan_client_flt_info[client_position].flt_rule_set_v4 = true;
		eth_bridge_wlan_client_flt_info[client_position].flt_rule_hdl_v4 = wlan_client_flt_rule_hdl_v4[i].rule_hdl;
	}
	else
	{
		eth_bridge_wlan_client_flt_info[client_position].flt_rule_set_v6 = true;
		eth_bridge_wlan_client_flt_info[client_position].flt_rule_hdl_v6 = wlan_client_flt_rule_hdl_v6[i].rule_hdl;
	}

fail:
	free(pFilteringTable);

	return res;
}

int IPACM_Lan::eth_bridge_del_wlan_client_flt_rule(uint8_t* mac)
{
	if(mac == NULL)
	{
		IPACMERR("Client MAC address is empty.\n");
		return IPACM_FAILURE;
	}

	IPACMDBG_H("Received WLAN client MAC 0x%02x%02x%02x%02x%02x%02x.\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	int i, j, res = IPACM_SUCCESS;
	for(i=0; i<wlan_client_flt_info_count; i++)
	{
		if(memcmp(eth_bridge_wlan_client_flt_info[i].mac, mac, sizeof(eth_bridge_wlan_client_flt_info[i].mac)) == 0)
		{
			break;
		}
	}

	if(i == wlan_client_flt_info_count)
	{
		IPACMERR("Do not find the wlan client.\n");
		return IPACM_FAILURE;
	}

	if(eth_bridge_wlan_client_flt_info[i].flt_rule_set_v4 == true)
	{
		if(reset_to_dummy_flt_rule(IPA_IP_v4, eth_bridge_wlan_client_flt_info[i].flt_rule_hdl_v4) == IPACM_SUCCESS)
		{
			for(j=0; j<IPA_LAN_TO_LAN_MAX_WLAN_CLIENT; j++)
			{
				if(wlan_client_flt_rule_hdl_v4[j].rule_hdl == eth_bridge_wlan_client_flt_info[i].flt_rule_hdl_v4)
				{
					wlan_client_flt_rule_hdl_v4[j].valid = true;
					break;
				}
			}
			if(j == IPA_LAN_TO_LAN_MAX_WLAN_CLIENT)
			{
				IPACMERR("Not finding the rule handle in handle pool.\n");
				return IPACM_FAILURE;
			}
		}
		else
		{
			IPACMERR("Failed to delete the wlan client specific flt rule.\n");
			return IPACM_FAILURE;
		}
	}
	if(eth_bridge_wlan_client_flt_info[i].flt_rule_set_v6 == true)
	{
		if(reset_to_dummy_flt_rule(IPA_IP_v6, eth_bridge_wlan_client_flt_info[i].flt_rule_hdl_v6) == IPACM_SUCCESS)
		{
			for(j=0; j<IPA_LAN_TO_LAN_MAX_WLAN_CLIENT; j++)
			{
				if(wlan_client_flt_rule_hdl_v6[j].rule_hdl == eth_bridge_wlan_client_flt_info[i].flt_rule_hdl_v6)
				{
					wlan_client_flt_rule_hdl_v6[j].valid = true;
					break;
				}
			}
			if(j == IPA_LAN_TO_LAN_MAX_WLAN_CLIENT)
			{
				IPACMERR("Not finding the rule handle in handle pool.\n");
				return IPACM_FAILURE;
			}
		}
		else
		{
			IPACMERR("Failed to delete the wlan client specific flt rule.\n");
			return IPACM_FAILURE;
		}
	}

	for(j=i+1; j<wlan_client_flt_info_count; j++)
	{
		memcpy(&(eth_bridge_wlan_client_flt_info[j-1]), &(eth_bridge_wlan_client_flt_info[j]), sizeof(eth_bridge_client_flt_info));
	}
	memset(&(eth_bridge_wlan_client_flt_info[wlan_client_flt_info_count-1]), 0, sizeof(eth_bridge_client_flt_info));
	wlan_client_flt_info_count--;

	return res;
}

int IPACM_Lan::eth_bridge_add_lan_client_flt_rule(uint8_t* mac, ipa_ip_type iptype)
{
	int i, len, res = IPACM_SUCCESS, client_position;
	struct ipa_flt_rule_mdfy flt_rule;
	struct ipa_ioc_mdfy_flt_rule* pFilteringTable = NULL;
	bool client_is_found = false;

	if (rx_prop == NULL)
	{
		IPACMDBG_H("No rx properties registered for iface %s\n", dev_name);
		return IPACM_FAILURE;
	}
	if (mac == NULL)
	{
		IPACMERR("MAC address is empty.\n");
		return IPACM_FAILURE;
	}
	if (IPACM_Lan::usb_to_cpe_hdr_proc_ctx.valid == false && IPACM_Lan::cpe_to_usb_hdr_proc_ctx.valid == false)
	{
		IPACMDBG_H("USB to CPE and CPE to USB hdr proc ctx have not been set, don't add client specific flt rule.\n");
		return IPACM_FAILURE;
	}

	IPACMDBG_H("Received LAN client MAC 0x%02x%02x%02x%02x%02x%02x. if_cat: %d  \n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat);

	for (i=0; i<lan_client_flt_info_count; i++)
	{
		if (memcmp(eth_bridge_lan_client_flt_info[i].mac, mac, sizeof(eth_bridge_lan_client_flt_info[i].mac)) == 0)
		{
			client_is_found = true;
			client_position = i;
			if ((iptype == IPA_IP_v4 && eth_bridge_lan_client_flt_info[i].flt_rule_set_v4 == true)
				|| (iptype == IPA_IP_v6 && eth_bridge_lan_client_flt_info[i].flt_rule_set_v6 == true))
			{
				IPACMDBG_H("Flt rule for iptype %d has been set.\n", iptype);
				return IPACM_SUCCESS;
			}
			break;
		}
	}

	if (client_is_found == false && lan_client_flt_info_count == IPA_LAN_TO_LAN_MAX_LAN_CLIENT)
	{
		IPACMDBG_H("The lan client flt table is already full.\n");
		return IPACM_FAILURE;
	}

	len = sizeof(struct ipa_ioc_mdfy_flt_rule) + sizeof(struct ipa_flt_rule_mdfy);
	pFilteringTable = (struct ipa_ioc_mdfy_flt_rule*)malloc(len);
	if (!pFilteringTable)
	{
		IPACMERR("Failed to allocate ipa_ioc_mdfy_flt_rule memory...\n");
		return IPACM_FAILURE;
	}
	memset(pFilteringTable, 0, len);

	/* add mac based rule*/
	pFilteringTable->commit = 1;
	pFilteringTable->ip = iptype;
	pFilteringTable->num_rules = 1;

	memset(&flt_rule, 0, sizeof(struct ipa_flt_rule_mdfy));
	flt_rule.status = -1;

	flt_rule.rule.retain_hdr = 0;
	flt_rule.rule.to_uc = 0;
	flt_rule.rule.action = IPA_PASS_TO_ROUTING;
	flt_rule.rule.eq_attrib_type = 0;

	/* point to LAN-LAN routing table */
	if (iptype == IPA_IP_v4)
	{
		if (false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_lan_v4))
		{
			IPACMERR("Failed to get routing table handle.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		flt_rule.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_lan_v4.hdl;
		IPACMDBG_H("LAN->LAN filter rule use table: %s\n", IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_lan_v4.name);
	}
	else
	{
		if (false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_lan_v6))
		{
			IPACMERR("Failed to get routing table handle.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
		flt_rule.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_lan_v6.hdl;
		IPACMDBG_H("LAN->LAN filter rule use table: %s\n", IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_lan_v6.name);
	}

	memcpy(&flt_rule.rule.attrib, &rx_prop->rx[0].attrib, sizeof(flt_rule.rule.attrib));

	if (IPACM_Lan::lan_hdr_type == IPA_HDR_L2_ETHERNET_II)
	{
		flt_rule.rule.attrib.attrib_mask |= IPA_FLT_MAC_DST_ADDR_ETHER_II;
	}
	else if (IPACM_Lan::lan_hdr_type == IPA_HDR_L2_802_3)
	{
		flt_rule.rule.attrib.attrib_mask |= IPA_FLT_MAC_DST_ADDR_802_3;
	}
	else
	{
		IPACMERR("LAN hdr type is not expected.\n");
		res = IPACM_FAILURE;
		goto fail;
	}

	memcpy(flt_rule.rule.attrib.dst_mac_addr, mac, sizeof(flt_rule.rule.attrib.dst_mac_addr));
	memset(flt_rule.rule.attrib.dst_mac_addr_mask, 0xFF, sizeof(flt_rule.rule.attrib.dst_mac_addr_mask));

	if (iptype == IPA_IP_v4)
	{
		for (i=0; i<IPA_LAN_TO_LAN_MAX_LAN_CLIENT; i++)
		{
			if (lan_client_flt_rule_hdl_v4[i].valid == true)
			{
				flt_rule.rule_hdl = lan_client_flt_rule_hdl_v4[i].rule_hdl;
				lan_client_flt_rule_hdl_v4[i].valid = false;
				break;
			}
		}
		if (i == IPA_LAN_TO_LAN_MAX_LAN_CLIENT)
		{
			IPACMDBG_H("Cannot find a valid flt rule hdl.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
	}
	else
	{
		for (i=0; i<IPA_LAN_TO_LAN_MAX_LAN_CLIENT; i++)
		{
			if (lan_client_flt_rule_hdl_v6[i].valid == true)
			{
				flt_rule.rule_hdl = lan_client_flt_rule_hdl_v6[i].rule_hdl;
				lan_client_flt_rule_hdl_v6[i].valid = false;
				break;
			}
		}
		if (i == IPA_LAN_TO_LAN_MAX_LAN_CLIENT)
		{
			IPACMDBG_H("Cannot find a valid flt rule hdl.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
	}

	memcpy(&(pFilteringTable->rules[0]), &flt_rule, sizeof(struct ipa_flt_rule_mdfy));

	if (false == m_filtering.ModifyFilteringRule(pFilteringTable))
	{
		IPACMERR("Failed to add wlan client filtering rules.\n");
		res = IPACM_FAILURE;
		goto fail;
	}

	if (client_is_found == false)
	{
		client_position = lan_client_flt_info_count;
		lan_client_flt_info_count++;
	}

	memcpy(eth_bridge_lan_client_flt_info[client_position].mac, mac, sizeof(eth_bridge_lan_client_flt_info[client_position].mac));

	if (iptype == IPA_IP_v4)
	{
		eth_bridge_lan_client_flt_info[client_position].flt_rule_set_v4 = true;
		eth_bridge_lan_client_flt_info[client_position].flt_rule_hdl_v4 = lan_client_flt_rule_hdl_v4[i].rule_hdl;
	}
	else
	{
		eth_bridge_lan_client_flt_info[client_position].flt_rule_set_v6 = true;
		eth_bridge_lan_client_flt_info[client_position].flt_rule_hdl_v6 = lan_client_flt_rule_hdl_v6[i].rule_hdl;
	}

fail:
	free(pFilteringTable);

	return res;
}

int IPACM_Lan::eth_bridge_del_lan_client_flt_rule(uint8_t* mac)
{
	if (mac == NULL)
	{
		IPACMERR("Client MAC address is empty.\n");
		return IPACM_FAILURE;
	}

	IPACMDBG_H("Received LAN client MAC 0x%02x%02x%02x%02x%02x%02x.\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	int i, j, res = IPACM_SUCCESS;
	for (i=0; i<lan_client_flt_info_count; i++)
	{
		if (memcmp(eth_bridge_lan_client_flt_info[i].mac, mac, sizeof(eth_bridge_lan_client_flt_info[i].mac)) == 0)
		{
			break;
		}
	}

	if (i == lan_client_flt_info_count)
	{
		IPACMERR("Do not find the lan client.\n");
		return IPACM_FAILURE;
	}

	if (eth_bridge_lan_client_flt_info[i].flt_rule_set_v4 == true)
	{
		if (reset_to_dummy_flt_rule(IPA_IP_v4, eth_bridge_lan_client_flt_info[i].flt_rule_hdl_v4) == IPACM_SUCCESS)
		{
			for (j=0; j<IPA_LAN_TO_LAN_MAX_LAN_CLIENT; j++)
			{
				if (lan_client_flt_rule_hdl_v4[j].rule_hdl == eth_bridge_lan_client_flt_info[i].flt_rule_hdl_v4)
				{
					lan_client_flt_rule_hdl_v4[j].valid = true;
					break;
				}
			}
			if (j == IPA_LAN_TO_LAN_MAX_LAN_CLIENT)
			{
				IPACMERR("Not finding the rule handle in handle pool.\n");
				return IPACM_FAILURE;
			}
		}
		else
		{
			IPACMERR("Failed to delete the lan client specific flt rule.\n");
			return IPACM_FAILURE;
		}
	}
	if (eth_bridge_lan_client_flt_info[i].flt_rule_set_v6 == true)
	{
		if (reset_to_dummy_flt_rule(IPA_IP_v6, eth_bridge_lan_client_flt_info[i].flt_rule_hdl_v6) == IPACM_SUCCESS)
		{
			for (j=0; j<IPA_LAN_TO_LAN_MAX_LAN_CLIENT; j++)
			{
				if (lan_client_flt_rule_hdl_v6[j].rule_hdl == eth_bridge_lan_client_flt_info[i].flt_rule_hdl_v6)
				{
					lan_client_flt_rule_hdl_v6[j].valid = true;
					break;
				}
			}
			if (j == IPA_LAN_TO_LAN_MAX_LAN_CLIENT)
			{
				IPACMERR("Not finding the rule handle in handle pool.\n");
				return IPACM_FAILURE;
			}
		}
		else
		{
			IPACMERR("Failed to delete the lan client specific flt rule.\n");
			return IPACM_FAILURE;
		}
	}

	for (j=i+1; j<lan_client_flt_info_count; j++)
	{
		memcpy(&(eth_bridge_lan_client_flt_info[j-1]), &(eth_bridge_lan_client_flt_info[j]), sizeof(eth_bridge_client_flt_info));
	}
	memset(&(eth_bridge_lan_client_flt_info[lan_client_flt_info_count-1]), 0, sizeof(eth_bridge_client_flt_info));
	lan_client_flt_info_count--;

	return res;
}

int IPACM_Lan::add_hdr_proc_ctx()
{
	int len, res = IPACM_SUCCESS;
	struct ipa_ioc_add_hdr_proc_ctx* pHeaderProcTable = NULL;

	len = sizeof(struct ipa_ioc_add_hdr_proc_ctx) + sizeof(struct ipa_hdr_proc_ctx_add);
	pHeaderProcTable = (ipa_ioc_add_hdr_proc_ctx*)malloc(len);
	if(pHeaderProcTable == NULL)
	{
		IPACMERR("Cannot allocate header processing table.\n");
		return IPACM_FAILURE;
	}

	if(IPACM_Lan::wlan_hdr_type != IPA_HDR_L2_NONE)
	{
		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == WLAN_IF)
		{
			if(IPACM_Lan::wlan_to_wlan_hdr_proc_ctx.valid == false)
			{
				memset(pHeaderProcTable, 0, len);
				pHeaderProcTable->commit = 1;
				pHeaderProcTable->num_proc_ctxs = 1;
				pHeaderProcTable->proc_ctx[0].type = get_hdr_proc_type(IPACM_Lan::wlan_hdr_type, IPACM_Lan::wlan_hdr_type);
				pHeaderProcTable->proc_ctx[0].hdr_hdl = IPACM_Lan::wlan_hdr_template_hdl;
				if (m_header.AddHeaderProcCtx(pHeaderProcTable) == false)
				{
					IPACMERR("Adding WLAN to WLAN hdr proc ctx failed with status: %d\n", pHeaderProcTable->proc_ctx[0].status);
					res = IPACM_FAILURE;
				}
				else
				{
					IPACM_Lan::wlan_to_wlan_hdr_proc_ctx.proc_ctx_hdl = pHeaderProcTable->proc_ctx[0].proc_ctx_hdl;
					IPACM_Lan::wlan_to_wlan_hdr_proc_ctx.valid = true;
					IPACMDBG_H("WLAN to WLAN hdr proc ctx is added successfully. \n");
				}
			}
		}

		if(IPACM_Lan::lan_hdr_type != IPA_HDR_L2_NONE)
		{
			if(IPACM_Lan::lan_to_wlan_hdr_proc_ctx.valid == false)
			{
				memset(pHeaderProcTable, 0, len);
				pHeaderProcTable->commit = 1;
				pHeaderProcTable->num_proc_ctxs = 1;
				pHeaderProcTable->proc_ctx[0].type = get_hdr_proc_type(IPACM_Lan::lan_hdr_type, IPACM_Lan::wlan_hdr_type);
				pHeaderProcTable->proc_ctx[0].hdr_hdl = IPACM_Lan::wlan_hdr_template_hdl;
				if (m_header.AddHeaderProcCtx(pHeaderProcTable) == false)
				{
					IPACMERR("Adding LAN to WLAN hdr proc ctx failed with status: %d\n", pHeaderProcTable->proc_ctx[0].status);
					res = IPACM_FAILURE;
					goto fail;
				}
				else
				{
					IPACM_Lan::lan_to_wlan_hdr_proc_ctx.proc_ctx_hdl = pHeaderProcTable->proc_ctx[0].proc_ctx_hdl;
					IPACM_Lan::lan_to_wlan_hdr_proc_ctx.valid = true;
					IPACMDBG_H("LAN to WLAN hdr proc ctx is added successfully. \n");
				}

				ipacm_cmd_q_data evt_data;
				memset(&evt_data, 0, sizeof(ipacm_cmd_q_data));

				ipacm_event_data_if_cat* cat;
				cat = (ipacm_event_data_if_cat*)malloc(sizeof(ipacm_event_data_if_cat));
				if(cat == NULL)
				{
					IPACMERR("Unable to allocate memory.\n");
					return IPACM_FAILURE;
				}
				memset(cat, 0, sizeof(ipacm_event_data_if_cat));
				cat->if_cat = IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat;

				evt_data.evt_data = cat;
				evt_data.event = IPA_ETH_BRIDGE_HDR_PROC_CTX_SET_EVENT;
				IPACMDBG_H("Posting event IPA_ETH_BRIDGE_HDR_PROC_CTX_SET_EVENT\n");
				IPACM_EvtDispatcher::PostEvt(&evt_data);
			}

			if(IPACM_Lan::is_usb_up == true && IPACM_Lan::wlan_to_usb_hdr_proc_ctx.valid == false)
			{
				memset(pHeaderProcTable, 0, len);
				pHeaderProcTable->commit = 1;
				pHeaderProcTable->num_proc_ctxs = 1;
				pHeaderProcTable->proc_ctx[0].type = get_hdr_proc_type(IPACM_Lan::wlan_hdr_type, IPACM_Lan::lan_hdr_type);
				pHeaderProcTable->proc_ctx[0].hdr_hdl = IPACM_Lan::usb_hdr_template_hdl;
				if (m_header.AddHeaderProcCtx(pHeaderProcTable) == false)
				{
					IPACMERR("Adding WLAN to USB hdr proc ctx failed with status: %d\n", pHeaderProcTable->proc_ctx[0].status);
					res = IPACM_FAILURE;
					goto fail;
				}
				else
				{
					IPACM_Lan::wlan_to_usb_hdr_proc_ctx.proc_ctx_hdl = pHeaderProcTable->proc_ctx[0].proc_ctx_hdl;
					IPACM_Lan::wlan_to_usb_hdr_proc_ctx.valid = true;
					IPACMDBG_H("WLAN to USB hdr proc ctx is added successfully. \n");
				}
			}

			if(IPACM_Lan::is_cpe_up == true && IPACM_Lan::wlan_to_cpe_hdr_proc_ctx.valid == false)
			{
				memset(pHeaderProcTable, 0, len);
				pHeaderProcTable->commit = 1;
				pHeaderProcTable->num_proc_ctxs = 1;
				pHeaderProcTable->proc_ctx[0].type = get_hdr_proc_type(IPACM_Lan::wlan_hdr_type, IPACM_Lan::lan_hdr_type);
				pHeaderProcTable->proc_ctx[0].hdr_hdl = IPACM_Lan::cpe_hdr_template_hdl;
				if (m_header.AddHeaderProcCtx(pHeaderProcTable) == false)
				{
					IPACMERR("Adding WLAN to CPE hdr proc ctx failed with status: %d\n", pHeaderProcTable->proc_ctx[0].status);
					res = IPACM_FAILURE;
					goto fail;
				}
				else
				{
					IPACM_Lan::wlan_to_cpe_hdr_proc_ctx.proc_ctx_hdl = pHeaderProcTable->proc_ctx[0].proc_ctx_hdl;
					IPACM_Lan::wlan_to_cpe_hdr_proc_ctx.valid = true;
					IPACMDBG_H("WLAN to CPE hdr proc ctx is added successfully. \n");

				}
			}
		}
	}

	if(IPACM_Lan::lan_hdr_type != IPA_HDR_L2_NONE)
	{
		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF ||
			(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF))
		{
			if(IPACM_Iface::ipacmcfg->ipacm_odu_router_mode == false)
			{
				IPACMDBG_H("ODU is in bridge mode, do not set CPE to USB and USB to CPE hdr proc ctx.\n");
				return res;
			}
			if(IPACM_Lan::is_cpe_up == true && IPACM_Lan::is_usb_up == true &&
				IPACM_Lan::cpe_to_usb_hdr_proc_ctx.valid == false && IPACM_Lan::usb_to_cpe_hdr_proc_ctx.valid == false)
			{
				memset(pHeaderProcTable, 0, len);
				pHeaderProcTable->commit = 1;
				pHeaderProcTable->num_proc_ctxs = 1;
				pHeaderProcTable->proc_ctx[0].type = get_hdr_proc_type(IPACM_Lan::lan_hdr_type, IPACM_Lan::lan_hdr_type);
				pHeaderProcTable->proc_ctx[0].hdr_hdl = IPACM_Lan::usb_hdr_template_hdl;
				if (m_header.AddHeaderProcCtx(pHeaderProcTable) == false)
				{
					IPACMERR("Adding CPE to USB hdr proc ctx failed with status: %d\n", pHeaderProcTable->proc_ctx[0].status);
					res = IPACM_FAILURE;
					goto fail;
				}
				else
				{
					IPACM_Lan::cpe_to_usb_hdr_proc_ctx.proc_ctx_hdl = pHeaderProcTable->proc_ctx[0].proc_ctx_hdl;
					IPACM_Lan::cpe_to_usb_hdr_proc_ctx.valid = true;
					IPACMDBG_H("CPE to USB hdr proc ctx is added successfully. \n");
				}

				pHeaderProcTable->commit = 1;
				pHeaderProcTable->num_proc_ctxs = 1;
				pHeaderProcTable->proc_ctx[0].type = get_hdr_proc_type(IPACM_Lan::lan_hdr_type, IPACM_Lan::lan_hdr_type);
				pHeaderProcTable->proc_ctx[0].hdr_hdl = IPACM_Lan::cpe_hdr_template_hdl;
				if (m_header.AddHeaderProcCtx(pHeaderProcTable) == false)
				{
					IPACMERR("Adding USB to CPE hdr proc ctx failed with status: %d\n", pHeaderProcTable->proc_ctx[0].status);
					res = IPACM_FAILURE;
					goto fail;
				}
				else
				{
					IPACM_Lan::usb_to_cpe_hdr_proc_ctx.proc_ctx_hdl = pHeaderProcTable->proc_ctx[0].proc_ctx_hdl;
					IPACM_Lan::usb_to_cpe_hdr_proc_ctx.valid = true;
					IPACMDBG_H("USB to CPE hdr proc ctx is added successfully. \n");
				}

				ipacm_cmd_q_data evt_data;
				memset(&evt_data, 0, sizeof(ipacm_cmd_q_data));

				ipacm_event_data_if_cat* cat;
				cat = (ipacm_event_data_if_cat*)malloc(sizeof(ipacm_event_data_if_cat));
				if(cat == NULL)
				{
					IPACMERR("Unable to allocate memory.\n");
					return IPACM_FAILURE;
				}
				memset(cat, 0, sizeof(ipacm_event_data_if_cat));
				cat->if_cat = IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat;

				evt_data.evt_data = cat;
				evt_data.event = IPA_ETH_BRIDGE_HDR_PROC_CTX_SET_EVENT;
				IPACMDBG_H("Posting event IPA_ETH_BRIDGE_HDR_PROC_CTX_SET_EVENT\n");
				IPACM_EvtDispatcher::PostEvt(&evt_data);
			}
		}
	}
	else
	{
		IPACMDBG_H("Not adding header processing context.\n");
	}

fail:
	free(pHeaderProcTable);
	return res;
}

ipa_hdr_proc_type IPACM_Lan::get_hdr_proc_type(ipa_hdr_l2_type t1, ipa_hdr_l2_type t2)
{
	if(t1 == IPA_HDR_L2_ETHERNET_II)
	{
		if(t2 == IPA_HDR_L2_ETHERNET_II)
		{
			return IPA_HDR_PROC_ETHII_TO_ETHII;
		}
		if(t2 == IPA_HDR_L2_802_3)
		{
			return IPA_HDR_PROC_ETHII_TO_802_3;
		}
	}

	if(t1 == IPA_HDR_L2_802_3)
	{
		if(t2 == IPA_HDR_L2_ETHERNET_II)
		{
			return IPA_HDR_PROC_802_3_TO_ETHII;
		}
		if(t2 == IPA_HDR_L2_802_3)
		{
			return IPA_HDR_PROC_802_3_TO_802_3;
		}
	}

	return IPA_HDR_PROC_NONE;
}

int IPACM_Lan::eth_bridge_install_cache_wlan_client_flt_rule(ipa_ip_type iptype)
{
	int i;

	IPACMDBG_H("There are %d wlan clients cached.\n",IPACM_Lan::num_wlan_client);
	for(i=0; i<IPACM_Lan::num_wlan_client; i++)
	{
		eth_bridge_add_wlan_client_flt_rule(IPACM_Lan::eth_bridge_wlan_client[i].mac, iptype);
	}
	return IPACM_SUCCESS;
}

int IPACM_Lan::eth_bridge_install_cache_lan_client_flt_rule(ipa_ip_type iptype)
{
	int i;

	IPACMDBG_H("There are %d lan clients cached.\n", IPACM_Lan::num_lan_client);
	for(i=0; i<IPACM_Lan::num_lan_client; i++)
	{
		if (IPACM_Lan::eth_bridge_lan_client[i].ipa_if_num != ipa_if_num)
		{
			eth_bridge_add_lan_client_flt_rule(IPACM_Lan::eth_bridge_lan_client[i].mac, iptype);
		}
	}
	return IPACM_SUCCESS;
}

int IPACM_Lan::eth_bridge_add_lan_client_rt_rule(uint8_t* mac, eth_bridge_src_iface src, ipa_ip_type iptype)
{
	if (tx_prop == NULL)
	{
		IPACMDBG_H("Tx prop is empty, not adding routing rule.\n");
		return IPACM_SUCCESS;
	}
	if (mac == NULL)
	{
		IPACMERR("Client MAC address is empty.\n");
		return IPACM_FAILURE;
	}

	IPACMDBG_H("Received client MAC 0x%02x%02x%02x%02x%02x%02x. src_iface: %d if_cat: %d \n",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], src, IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat );

	if (src == SRC_WLAN && IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF
			&& IPACM_Lan::wlan_to_usb_hdr_proc_ctx.valid == false)
	{
		IPACMDBG_H("WLAN to USB hdr proc ctx has not been set, don't add USB routing rule.\n");
		return IPACM_FAILURE;
	}
	if (src == SRC_WLAN && IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF
			&& IPACM_Lan::wlan_to_cpe_hdr_proc_ctx.valid == false)
	{
		IPACMDBG_H("WLAN to CPE hdr proc ctx has not been set, don't add CPE routing rule.\n");
		return IPACM_FAILURE;
	}

	if (src == SRC_LAN && IPACM_Lan::usb_to_cpe_hdr_proc_ctx.valid == false
			&& IPACM_Lan::cpe_to_usb_hdr_proc_ctx.valid == false)
	{
		IPACMDBG_H("hdr proc ctx between USB and CPE has not been set, don't add routing rule.\n");
		return IPACM_FAILURE;
	}

	if(iptype == IPA_IP_v4)
	{
		if((src == SRC_LAN && lan_client_rt_from_lan_info_count_v4 == IPA_LAN_TO_LAN_MAX_LAN_CLIENT)
			|| (src == SRC_WLAN && lan_client_rt_from_wlan_info_count_v4 == IPA_LAN_TO_LAN_MAX_WLAN_CLIENT))
		{
			IPACMDBG_H("client number has reached maximum.\n");
			return IPACM_FAILURE;
		}
	}
	else
	{
		if ((src == SRC_LAN && lan_client_rt_from_lan_info_count_v6 == IPA_LAN_TO_LAN_MAX_LAN_CLIENT)
			|| (src == SRC_WLAN && lan_client_rt_from_wlan_info_count_v6 == IPA_LAN_TO_LAN_MAX_WLAN_CLIENT))
		{
			IPACMDBG_H("client number has reached maximum.\n");
			return IPACM_FAILURE;
		}
	}

	int i, len, res = IPACM_SUCCESS;
	struct ipa_ioc_add_rt_rule* rt_rule_table = NULL;
	struct ipa_rt_rule_add rt_rule;
	int position, num_rt_rule;

	if (src == SRC_LAN)
	{
		if (iptype == IPA_IP_v4)
		{
			for (i=0; i<lan_client_rt_from_lan_info_count_v4; i++)
			{
				if (memcmp(eth_bridge_get_client_rt_info_ptr(i, src, iptype)->mac, mac, sizeof(eth_bridge_get_client_rt_info_ptr(i, src, iptype)->mac)) == 0)
				{
					IPACMDBG_H("The client's routing rule was added before.\n");
					return IPACM_SUCCESS;
				}
			}
			memcpy(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_lan_info_count_v4, src, iptype)->mac, mac,
					sizeof(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_lan_info_count_v4, src, iptype)->mac));
		}
		else
		{
			for (i=0; i<lan_client_rt_from_lan_info_count_v6; i++)
			{
				if (memcmp(eth_bridge_get_client_rt_info_ptr(i, src, iptype)->mac, mac, sizeof(eth_bridge_get_client_rt_info_ptr(i, src, iptype)->mac)) == 0)
				{
					IPACMDBG_H("The client's routing rule was added before.\n");
					return IPACM_SUCCESS;
				}
			}
			memcpy(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_lan_info_count_v6, src, iptype)->mac, mac,
					sizeof(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_lan_info_count_v6, src, iptype)->mac));
		}
	}
	else
	{
		if (iptype == IPA_IP_v4)
		{
			for (i=0; i<lan_client_rt_from_wlan_info_count_v4; i++)
			{
				if (memcmp(eth_bridge_get_client_rt_info_ptr(i, src, iptype)->mac, mac, sizeof(eth_bridge_get_client_rt_info_ptr(i, src, iptype)->mac)) == 0)
				{
					IPACMDBG_H("The client's routing rule was added before.\n");
					return IPACM_SUCCESS;
				}
			}
			memcpy(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_wlan_info_count_v4, src, iptype)->mac, mac,
					sizeof(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_wlan_info_count_v4, src, iptype)->mac));
		}
		else
		{
			for (i=0; i<lan_client_rt_from_wlan_info_count_v6; i++)
			{
				if (memcmp(eth_bridge_get_client_rt_info_ptr(i, src, iptype)->mac, mac, sizeof(eth_bridge_get_client_rt_info_ptr(i, src, iptype)->mac)) == 0)
				{
					IPACMDBG_H("The client's routing rule was added before.\n");
					return IPACM_SUCCESS;
				}
			}
			memcpy(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_wlan_info_count_v6, src, iptype)->mac, mac,
					sizeof(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_wlan_info_count_v6, src, iptype)->mac));
		}
	}

	if (iptype == IPA_IP_v4)
	{
		num_rt_rule = each_client_rt_rule_count_v4;
	}
	else
	{
		num_rt_rule = each_client_rt_rule_count_v6;
	}

	len = sizeof(ipa_ioc_add_rt_rule) + num_rt_rule * sizeof(ipa_rt_rule_add);
	rt_rule_table = (ipa_ioc_add_rt_rule*)malloc(len);
	if (rt_rule_table == NULL)
	{
		IPACMERR("Failed to allocate memory.\n");
		return IPACM_FAILURE;
	}
	memset(rt_rule_table, 0, len);

	rt_rule_table->commit = 1;
	rt_rule_table->ip = iptype;
	rt_rule_table->num_rules = num_rt_rule;
	if (src == SRC_LAN)
	{
		if (iptype == IPA_IP_v4)
		{
			strlcpy(rt_rule_table->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_lan_v4.name, sizeof(rt_rule_table->rt_tbl_name));
		}
		else
		{
			strlcpy(rt_rule_table->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_lan_v6.name, sizeof(rt_rule_table->rt_tbl_name));
		}
	}
	else
	{
		if (iptype == IPA_IP_v4)
		{
			strlcpy(rt_rule_table->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_wlan_v4.name, sizeof(rt_rule_table->rt_tbl_name));
		}
		else
		{
			strlcpy(rt_rule_table->rt_tbl_name, IPACM_Iface::ipacmcfg->rt_tbl_eth_bridge_lan_wlan_v6.name, sizeof(rt_rule_table->rt_tbl_name));
		}
	}
	rt_rule_table->rt_tbl_name[IPA_RESOURCE_NAME_MAX-1] = '\0';
	memset(&rt_rule, 0, sizeof(ipa_rt_rule_add));
	rt_rule.at_rear = false;
	rt_rule.status = -1;
	rt_rule.rt_rule_hdl = -1;

	rt_rule.rule.hdr_hdl = 0;
	if(src == SRC_LAN)
	{
		if (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
		{
			rt_rule.rule.hdr_proc_ctx_hdl = IPACM_Lan::cpe_to_usb_hdr_proc_ctx.proc_ctx_hdl;
		}
		else if (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
		{
			rt_rule.rule.hdr_proc_ctx_hdl = IPACM_Lan::usb_to_cpe_hdr_proc_ctx.proc_ctx_hdl;
		}
		else
		{
			IPACMERR("Iface category is not expected.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
	}
	else
	{
		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
		{
			rt_rule.rule.hdr_proc_ctx_hdl = IPACM_Lan::wlan_to_usb_hdr_proc_ctx.proc_ctx_hdl;
		}
		else if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF)
		{
			rt_rule.rule.hdr_proc_ctx_hdl = IPACM_Lan::wlan_to_cpe_hdr_proc_ctx.proc_ctx_hdl;
		}
		else
		{
			IPACMERR("Iface category is not expected.\n");
			res = IPACM_FAILURE;
			goto fail;
		}
	}
	position = 0;
	for(i=0; i<iface_query->num_tx_props; i++)
	{
		if(tx_prop->tx[i].ip == iptype)
		{
			if(position >= num_rt_rule)
			{
				IPACMERR("Number of routing rules already exceeds limit.\n");
				res = IPACM_FAILURE;
				goto fail;
			}
			rt_rule.rule.dst = tx_prop->tx[i].dst_pipe;

			memcpy(&rt_rule.rule.attrib, &tx_prop->tx[i].attrib, sizeof(rt_rule.rule.attrib));
			if(src == SRC_WLAN)	//src is WLAN means packet is from WLAN
			{
				if(IPACM_Lan::wlan_hdr_type == IPA_HDR_L2_ETHERNET_II)
				{
					rt_rule.rule.attrib.attrib_mask |= IPA_FLT_MAC_DST_ADDR_ETHER_II;
				}
				else
				{
					rt_rule.rule.attrib.attrib_mask |= IPA_FLT_MAC_DST_ADDR_802_3;
				}
			}
			else	//packet is from LAN
			{
				if(IPACM_Lan::lan_hdr_type == IPA_HDR_L2_ETHERNET_II)
				{
					rt_rule.rule.attrib.attrib_mask |= IPA_FLT_MAC_DST_ADDR_ETHER_II;
				}
				else
				{
					rt_rule.rule.attrib.attrib_mask |= IPA_FLT_MAC_DST_ADDR_802_3;
				}
			}
			memcpy(rt_rule.rule.attrib.dst_mac_addr, mac, sizeof(rt_rule.rule.attrib.dst_mac_addr));
			memset(rt_rule.rule.attrib.dst_mac_addr_mask, 0xFF, sizeof(rt_rule.rule.attrib.dst_mac_addr_mask));

			memcpy(&(rt_rule_table->rules[position]), &rt_rule, sizeof(rt_rule_table->rules[position]));
			position++;
		}
	}
	if(false == m_routing.AddRoutingRule(rt_rule_table))
	{
		IPACMERR("Routing rule addition failed!\n");
		res = IPACM_FAILURE;
		goto fail;
	}
	else
	{
		if(src == SRC_LAN)
		{
			for(i=0; i<num_rt_rule; i++)
			{
				if(iptype == IPA_IP_v4)
				{
					eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_lan_info_count_v4, src, iptype)->rt_rule_hdl[i] = rt_rule_table->rules[i].rt_rule_hdl;
				}
				else
				{
					eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_lan_info_count_v6, src, iptype)->rt_rule_hdl[i] = rt_rule_table->rules[i].rt_rule_hdl;
				}
			}
			if(iptype == IPA_IP_v4)
			{
				lan_client_rt_from_lan_info_count_v4++;
				IPACMDBG_H("Now the number of IPv4 rt rule on lan-lan rt table is %d.\n", lan_client_rt_from_lan_info_count_v4);
			}
			else
			{
				lan_client_rt_from_lan_info_count_v6++;
				IPACMDBG_H("Now the number of IPv6 rt rule on lan-lan rt table is %d.\n", lan_client_rt_from_lan_info_count_v6);
			}
		}
		else
		{
			for(i=0; i<num_rt_rule; i++)
			{
				if(iptype == IPA_IP_v4)
				{
					eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_wlan_info_count_v4, src, iptype)->rt_rule_hdl[i] = rt_rule_table->rules[i].rt_rule_hdl;
				}
				else
				{
					eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_wlan_info_count_v6, src, iptype)->rt_rule_hdl[i] = rt_rule_table->rules[i].rt_rule_hdl;
				}
			}
			if(iptype == IPA_IP_v4)
			{
				lan_client_rt_from_wlan_info_count_v4++;
				IPACMDBG_H("Now the number of IPv4 rt rule on lan-wlan rt table is %d.\n", lan_client_rt_from_wlan_info_count_v4);
			}
			else
			{
				lan_client_rt_from_wlan_info_count_v6++;
				IPACMDBG_H("Now the number of IPv6 rt rule on lan-wlan rt table is %d.\n", lan_client_rt_from_wlan_info_count_v6);
			}
		}
	}

fail:
	if(rt_rule_table != NULL)
	{
		free(rt_rule_table);
	}
	return res;
}

int IPACM_Lan::eth_bridge_del_lan_client_rt_rule(uint8_t* mac, eth_bridge_src_iface src)
{
	if(tx_prop == NULL)
	{
		IPACMDBG_H("Tx prop is empty, not deleting routing rule.\n");
		return IPACM_SUCCESS;
	}
	if(mac == NULL)
	{
		IPACMERR("Client MAC address is empty.\n");
		return IPACM_FAILURE;
	}

	IPACMDBG_H("Received client MAC 0x%02x%02x%02x%02x%02x%02x. src_iface: %d \n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], src);

	int i, position;

	/* first delete the rt rules from IPv4 rt table*/
	if(src == SRC_LAN)
	{
		for(i=0; i<lan_client_rt_from_lan_info_count_v4; i++)
		{
			if(memcmp(eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v4)->mac, mac, sizeof(eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v4)->mac)) == 0)
			{
				position = i;
				IPACMDBG_H("The client is found at position %d.\n", position);
				break;
			}
		}
		if(i == lan_client_rt_from_lan_info_count_v4)
		{
			IPACMERR("The client is not found.\n");
			return IPACM_FAILURE;
		}
	}
	else
	{
		for(i=0; i<lan_client_rt_from_wlan_info_count_v4; i++)
		{
			if(memcmp(eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v4)->mac, mac, sizeof(eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v4)->mac)) == 0)
			{
				position = i;
				IPACMDBG_H("The client is found at position %d.\n", position);
				break;
			}
		}
		if(i == lan_client_rt_from_wlan_info_count_v4)
		{
			IPACMERR("The client is not found.\n");
			return IPACM_FAILURE;
		}
	}

	for(i=0; i<each_client_rt_rule_count_v4; i++)
	{
		if(m_routing.DeleteRoutingHdl(eth_bridge_get_client_rt_info_ptr(position, src, IPA_IP_v4)->rt_rule_hdl[i], IPA_IP_v4) == false)
		{
			IPACMERR("Failed to delete routing rule %d.\n", i);
			return IPACM_FAILURE;
		}
	}

	if(src == SRC_LAN)
	{
		for(i=position+1; i<lan_client_rt_from_lan_info_count_v4; i++)
		{
			memcpy(eth_bridge_get_client_rt_info_ptr(i-1, src, IPA_IP_v4), eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v4), client_rt_info_size_v4);
		}
		memset(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_lan_info_count_v4-1, src, IPA_IP_v4), 0, client_rt_info_size_v4);
		lan_client_rt_from_lan_info_count_v4--;
		IPACMDBG_H("Now the number of IPv4 rt rule from lan info is %d.\n", lan_client_rt_from_lan_info_count_v4);
	}
	else
	{
		for(i=position+1; i<lan_client_rt_from_wlan_info_count_v4; i++)
		{
			memcpy(eth_bridge_get_client_rt_info_ptr(i-1, src, IPA_IP_v4), eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v4), client_rt_info_size_v4);
		}
		memset(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_wlan_info_count_v4-1, src, IPA_IP_v4), 0, client_rt_info_size_v4);
		lan_client_rt_from_wlan_info_count_v4--;
		IPACMDBG_H("Now the number of IPv4 rt rule from wlan info is %d.\n", lan_client_rt_from_wlan_info_count_v4);
	}

	/*delete rt rules from IPv6 rt table */
	if(src == SRC_LAN)
	{
		for(i=0; i<lan_client_rt_from_lan_info_count_v6; i++)
		{
			if(memcmp(eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v6)->mac, mac, sizeof(eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v6)->mac)) == 0)
			{
				position = i;
				IPACMDBG_H("The client is found at position %d.\n", position);
				break;
			}
		}
		if(i == lan_client_rt_from_lan_info_count_v6)
		{
			IPACMERR("The client is not found.\n");
			return IPACM_FAILURE;
		}
	}
	else
	{
		for(i=0; i<lan_client_rt_from_wlan_info_count_v6; i++)
		{
			if(memcmp(eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v6)->mac, mac, sizeof(eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v6)->mac)) == 0)
			{
				position = i;
				IPACMDBG_H("The client is found at position %d.\n", position);
				break;
			}
		}
		if(i == lan_client_rt_from_wlan_info_count_v6)
		{
			IPACMERR("The client is not found.\n");
			return IPACM_FAILURE;
		}
	}

	for(i=0; i<each_client_rt_rule_count_v6; i++)
	{
		if(m_routing.DeleteRoutingHdl(eth_bridge_get_client_rt_info_ptr(position, src, IPA_IP_v6)->rt_rule_hdl[i], IPA_IP_v6) == false)
		{
			IPACMERR("Failed to delete routing rule %d.\n", i);
			return IPACM_FAILURE;
		}
	}

	if(src == SRC_LAN)
	{
		for(i=position+1; i<lan_client_rt_from_lan_info_count_v6; i++)
		{
			memcpy(eth_bridge_get_client_rt_info_ptr(i-1, src, IPA_IP_v6), eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v6), client_rt_info_size_v6);
		}
		memset(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_lan_info_count_v6-1, src, IPA_IP_v6), 0, client_rt_info_size_v6);
		lan_client_rt_from_lan_info_count_v6--;
		IPACMDBG_H("Now the number of IPv6 rt rule from lan info is %d.\n", lan_client_rt_from_lan_info_count_v6);
	}
	else
	{
		for(i=position+1; i<lan_client_rt_from_wlan_info_count_v6; i++)
		{
			memcpy(eth_bridge_get_client_rt_info_ptr(i-1, src, IPA_IP_v6), eth_bridge_get_client_rt_info_ptr(i, src, IPA_IP_v6), client_rt_info_size_v6);
		}
		memset(eth_bridge_get_client_rt_info_ptr(lan_client_rt_from_wlan_info_count_v6-1, src, IPA_IP_v6), 0, client_rt_info_size_v6);
		lan_client_rt_from_wlan_info_count_v6--;
		IPACMDBG_H("Now the number of IPv6 rt rule from wlan info is %d.\n", lan_client_rt_from_wlan_info_count_v6);
	}

	return IPACM_SUCCESS;
}

eth_bridge_client_rt_info* IPACM_Lan::eth_bridge_get_client_rt_info_ptr(uint8_t index, eth_bridge_src_iface src, ipa_ip_type iptype)
{
	void* result;

	if(src == SRC_LAN)
	{
		if(iptype == IPA_IP_v4)
		{
			result = (void*)((void*)eth_bridge_lan_client_rt_from_lan_info_v4 + index * client_rt_info_size_v4);
		}
		else
		{
			result = (void*)((void*)eth_bridge_lan_client_rt_from_lan_info_v6 + index * client_rt_info_size_v6);
		}
	}
	else
	{
		if(iptype == IPA_IP_v4)
		{
			result = (void*)((void*)eth_bridge_lan_client_rt_from_wlan_info_v4 + index * client_rt_info_size_v4);
		}
		else
		{
			result = (void*)((void*)eth_bridge_lan_client_rt_from_wlan_info_v6 + index * client_rt_info_size_v6);
		}
	}
	return (eth_bridge_client_rt_info*)result;
}


void IPACM_Lan::eth_bridge_add_lan_client(uint8_t* mac)
{
	if(IPACM_Lan::num_lan_client == IPA_LAN_TO_LAN_MAX_LAN_CLIENT)
	{
		IPACMDBG_H("USB client table is already full.\n");
		return;
	}

	if(mac == NULL)
	{
		IPACMERR("Mac address is empty.\n");
		return;
	}

	int i;
	for(i=0; i<IPACM_Lan::num_lan_client; i++)
	{
		if(memcmp(IPACM_Lan::eth_bridge_lan_client[i].mac, mac, sizeof(IPACM_Lan::eth_bridge_lan_client[i].mac)) == 0)
		{
			IPACMDBG_H("The lan client mac has been added before at position %d.\n", i);
			return;
		}
	}

	memcpy(IPACM_Lan::eth_bridge_lan_client[IPACM_Lan::num_lan_client].mac, mac, sizeof(IPACM_Lan::eth_bridge_lan_client[IPACM_Lan::num_lan_client].mac));
	IPACM_Lan::eth_bridge_lan_client[IPACM_Lan::num_lan_client].ipa_if_num = ipa_if_num;
	IPACM_Lan::num_lan_client++;
	IPACMDBG_H("Now total num of lan clients is %d\n", IPACM_Lan::num_lan_client);
	return;
}

void IPACM_Lan::eth_bridge_del_lan_client(uint8_t* mac)
{
	if(mac == NULL)
	{
		IPACMERR("Mac address is empty.\n");
		return;
	}

	int i, j;
	for(i=0; i<IPACM_Lan::num_lan_client; i++)
	{
		if(memcmp(IPACM_Lan::eth_bridge_lan_client[i].mac, mac, sizeof(IPACM_Lan::eth_bridge_lan_client[i].mac)) == 0)
		{
			IPACMDBG_H("Found LAN client at position %d.\n", i);
			break;
		}
	}

	if(i == IPACM_Lan::num_lan_client)
	{
		IPACMDBG_H("Not finding the LAN client.\n");
		return;
	}

	for(j=i+1; j<IPACM_Lan::num_lan_client; j++)
	{
		memcpy(IPACM_Lan::eth_bridge_lan_client[j-1].mac, IPACM_Lan::eth_bridge_lan_client[j].mac, sizeof(IPACM_Lan::eth_bridge_lan_client[j].mac));
		IPACM_Lan::eth_bridge_lan_client[j-1].ipa_if_num = IPACM_Lan::eth_bridge_lan_client[j].ipa_if_num;
	}
	IPACM_Lan::num_lan_client--;
	IPACMDBG_H("Now total num of lan clients is %d\n", IPACM_Lan::num_lan_client);
	return;
}

int IPACM_Lan::eth_bridge_get_hdr_template_hdl(uint32_t* hdr_hdl)
{
	if(hdr_hdl == NULL)
	{
		IPACMDBG_H("Hdr handle pointer is empty.\n");
		return IPACM_FAILURE;
	}

	struct ipa_ioc_get_hdr hdr;
	memset(&hdr, 0, sizeof(hdr));

	memcpy(hdr.name, tx_prop->tx[0].hdr_name, sizeof(hdr.name));
	if(m_header.GetHeaderHandle(&hdr) == false)
	{
		IPACMERR("Failed to get template hdr hdl.\n");
		return IPACM_FAILURE;
	}

	*hdr_hdl = hdr.hdl;
	return IPACM_SUCCESS;
}

int IPACM_Lan::del_hdr_proc_ctx()
{
	if(IPACM_Lan::lan_to_wlan_hdr_proc_ctx.valid == true)
	{
		if((IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == WLAN_IF)
			|| (IPACM_Lan::is_usb_up == false && IPACM_Lan::is_cpe_up == false))
		{
			if(m_header.DeleteHeaderProcCtx(IPACM_Lan::lan_to_wlan_hdr_proc_ctx.proc_ctx_hdl) == false)
			{
				IPACMERR("Failed to delete usb to wlan hdr proc ctx.\n");
				return IPACM_FAILURE;
			}
			IPACM_Lan::lan_to_wlan_hdr_proc_ctx.valid = false;

			ipacm_cmd_q_data evt_data;
			memset(&evt_data, 0, sizeof(ipacm_cmd_q_data));

			ipacm_event_data_if_cat* cat;
			cat = (ipacm_event_data_if_cat*)malloc(sizeof(ipacm_event_data_if_cat));
			if(cat == NULL)
			{
				IPACMERR("Unable to allocate memory.\n");
				return IPACM_FAILURE;
			}
			memset(cat, 0, sizeof(ipacm_event_data_if_cat));
			cat->if_cat = IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat;

			evt_data.evt_data = cat;
			evt_data.event = IPA_ETH_BRIDGE_HDR_PROC_CTX_UNSET_EVENT;
			IPACMDBG_H("Posting event IPA_ETH_BRIDGE_HDR_PROC_CTX_UNSET_EVENT\n");
			IPACM_EvtDispatcher::PostEvt(&evt_data);
		}
	}

	if(IPACM_Lan::wlan_to_usb_hdr_proc_ctx.valid == true)
	{
		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == WLAN_IF || IPACM_Lan::is_usb_up == false)
		{
			if(m_header.DeleteHeaderProcCtx(IPACM_Lan::wlan_to_usb_hdr_proc_ctx.proc_ctx_hdl) == false)
			{
				IPACMERR("Failed to delete wlan to usb hdr proc ctx.\n");
				return IPACM_FAILURE;
			}
			IPACM_Lan::wlan_to_usb_hdr_proc_ctx.valid = false;
		}
	}

	if(IPACM_Lan::wlan_to_cpe_hdr_proc_ctx.valid == true)
	{
		if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == WLAN_IF || IPACM_Lan::is_cpe_up == false)
		{
			if(m_header.DeleteHeaderProcCtx(IPACM_Lan::wlan_to_cpe_hdr_proc_ctx.proc_ctx_hdl) == false)
			{
				IPACMERR("Failed to delete wlan to cpe hdr proc ctx.\n");
				return IPACM_FAILURE;
			}
			IPACM_Lan::wlan_to_cpe_hdr_proc_ctx.valid = false;
		}
	}

	if(IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == WLAN_IF)
	{
		if(IPACM_Lan::wlan_to_wlan_hdr_proc_ctx.valid == true)
		{
			if(m_header.DeleteHeaderProcCtx(IPACM_Lan::wlan_to_wlan_hdr_proc_ctx.proc_ctx_hdl) == false)
			{
				IPACMERR("Failed to delete wlan to wlan hdr proc ctx.\n");
				return IPACM_FAILURE;
			}
			IPACM_Lan::wlan_to_wlan_hdr_proc_ctx.valid = false;
		}
	}

	if(IPACM_Lan::cpe_to_usb_hdr_proc_ctx.valid == true && IPACM_Lan::usb_to_cpe_hdr_proc_ctx.valid == true)
	{
		if((IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == LAN_IF)
			|| (IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat == ODU_IF))
		{
			if(m_header.DeleteHeaderProcCtx(IPACM_Lan::cpe_to_usb_hdr_proc_ctx.proc_ctx_hdl) == false)
			{
				IPACMERR("Failed to delete cpe to usb hdr proc ctx.\n");
				return IPACM_FAILURE;
			}
			IPACM_Lan::cpe_to_usb_hdr_proc_ctx.valid = false;

			if(m_header.DeleteHeaderProcCtx(IPACM_Lan::usb_to_cpe_hdr_proc_ctx.proc_ctx_hdl) == false)
			{
				IPACMERR("Failed to delete usb to cpe hdr proc ctx.\n");
				return IPACM_FAILURE;
			}
			IPACM_Lan::usb_to_cpe_hdr_proc_ctx.valid = false;

			ipacm_cmd_q_data evt_data;
			memset(&evt_data, 0, sizeof(ipacm_cmd_q_data));

			ipacm_event_data_if_cat* cat;
			cat = (ipacm_event_data_if_cat*)malloc(sizeof(ipacm_event_data_if_cat));
			if(cat == NULL)
			{
				IPACMERR("Unable to allocate memory.\n");
				return IPACM_FAILURE;
			}
			memset(cat, 0, sizeof(ipacm_event_data_if_cat));
			cat->if_cat = IPACM_Iface::ipacmcfg->iface_table[ipa_if_num].if_cat;

			evt_data.evt_data = cat;
			evt_data.event = IPA_ETH_BRIDGE_HDR_PROC_CTX_UNSET_EVENT;
			IPACMDBG_H("Posting event IPA_ETH_BRIDGE_HDR_PROC_CTX_UNSET_EVENT\n");
			IPACM_EvtDispatcher::PostEvt(&evt_data);
		}
	}
	return IPACM_SUCCESS;
}

int IPACM_Lan::handle_cradle_wan_mode_switch(bool is_wan_bridge_mode)
{
	struct ipa_flt_rule_mdfy flt_rule_entry;
	int len = 0;
	ipa_ioc_mdfy_flt_rule *m_pFilteringTable;

	IPACMDBG_H("Handle wan mode swtich: is wan bridge mode?%d\n", is_wan_bridge_mode);

	if (rx_prop == NULL)
	{
		IPACMDBG_H("No rx properties registered for iface %s\n", dev_name);
		return IPACM_SUCCESS;
	}

	len = sizeof(struct ipa_ioc_mdfy_flt_rule) + (1 * sizeof(struct ipa_flt_rule_mdfy));
	m_pFilteringTable = (struct ipa_ioc_mdfy_flt_rule *)calloc(1, len);
	if (m_pFilteringTable == NULL)
	{
		PERROR("Error Locate ipa_ioc_mdfy_flt_rule memory...\n");
		return IPACM_FAILURE;
	}

	m_pFilteringTable->commit = 1;
	m_pFilteringTable->ip = IPA_IP_v4;
	m_pFilteringTable->num_rules = (uint8_t)1;

	IPACMDBG_H("Retrieving routing hanle for table: %s\n",
					 IPACM_Iface::ipacmcfg->rt_tbl_wan_v4.name);
	if (false == m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_wan_v4))
	{
		IPACMERR("m_routing.GetRoutingTable(&IPACM_Iface::ipacmcfg->rt_tbl_wan_v4=0x%p) Failed.\n",
						 &IPACM_Iface::ipacmcfg->rt_tbl_wan_v4);
		free(m_pFilteringTable);
		return IPACM_FAILURE;
	}
	IPACMDBG_H("Routing handle for table: %d\n", IPACM_Iface::ipacmcfg->rt_tbl_wan_v4.hdl);


	memset(&flt_rule_entry, 0, sizeof(struct ipa_flt_rule_mdfy)); // Zero All Fields
	flt_rule_entry.status = -1;
	flt_rule_entry.rule_hdl = lan_wan_fl_rule_hdl[0];

	flt_rule_entry.rule.retain_hdr = 0;
	flt_rule_entry.rule.to_uc = 0;
	flt_rule_entry.rule.eq_attrib_type = 0;
	if(is_wan_bridge_mode)
	{
		flt_rule_entry.rule.action = IPA_PASS_TO_ROUTING;
	}
	else
	{
		flt_rule_entry.rule.action = IPA_PASS_TO_SRC_NAT;
	}
	flt_rule_entry.rule.rt_tbl_hdl = IPACM_Iface::ipacmcfg->rt_tbl_wan_v4.hdl;

	memcpy(&flt_rule_entry.rule.attrib,
				 &rx_prop->rx[0].attrib,
				 sizeof(flt_rule_entry.rule.attrib));

	flt_rule_entry.rule.attrib.attrib_mask |= IPA_FLT_DST_ADDR;
	flt_rule_entry.rule.attrib.u.v4.dst_addr_mask = 0x0;
	flt_rule_entry.rule.attrib.u.v4.dst_addr = 0x0;

	memcpy(&m_pFilteringTable->rules[0], &flt_rule_entry, sizeof(flt_rule_entry));
	if (false == m_filtering.ModifyFilteringRule(m_pFilteringTable))
	{
		IPACMERR("Error Modifying RuleTable(0) to Filtering, aborting...\n");
		free(m_pFilteringTable);
		return IPACM_FAILURE;
	}
	else
	{
		IPACMDBG_H("flt rule hdl = %d, status = %d\n",
						 m_pFilteringTable->rules[0].rule_hdl,
						 m_pFilteringTable->rules[0].status);
	}
	free(m_pFilteringTable);
	return IPACM_SUCCESS;
}

/*handle reset usb-client rt-rules */
int IPACM_Lan::handle_tethering_stats_event(ipa_get_data_stats_resp_msg_v01 *data)
{
	int cnt, pipe_len, fd;
	uint64_t num_ul_packets, num_ul_bytes;
	uint64_t num_dl_packets, num_dl_bytes;
	bool ul_pipe_found, dl_pipe_found;
	FILE *fp = NULL;

	fd = open(IPA_DEVICE_NAME, O_RDWR);
	if (fd < 0)
	{
		IPACMERR("Failed opening %s.\n", IPA_DEVICE_NAME);
		return IPACM_FAILURE;
	}


	ul_pipe_found = false;
	dl_pipe_found = false;
	num_ul_packets = 0;
	num_dl_packets = 0;
	num_ul_bytes = 0;
	num_dl_bytes = 0;

	if (data->dl_dst_pipe_stats_list_valid)
	{
		if(tx_prop != NULL)
		{
			for (pipe_len = 0; pipe_len < data->dl_dst_pipe_stats_list_len; pipe_len++)
			{
				IPACMDBG_H("Check entry(%d) dl_dst_pipe(%d)\n", pipe_len, data->dl_dst_pipe_stats_list[pipe_len].pipe_index);
				for (cnt=0; cnt<tx_prop->num_tx_props; cnt++)
				{
					IPACMDBG_H("Check Tx_prop_entry(%d) pipe(%d)\n", cnt, ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, tx_prop->tx[cnt].dst_pipe));
					if(ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, tx_prop->tx[cnt].dst_pipe) == data->dl_dst_pipe_stats_list[pipe_len].pipe_index)
					{
						/* update the DL stats */
						dl_pipe_found = true;
						num_dl_packets += data->dl_dst_pipe_stats_list[pipe_len].num_ipv4_packets;
						num_dl_packets += data->dl_dst_pipe_stats_list[pipe_len].num_ipv6_packets;
						num_dl_bytes += data->dl_dst_pipe_stats_list[pipe_len].num_ipv4_bytes;
						num_dl_bytes += data->dl_dst_pipe_stats_list[pipe_len].num_ipv6_bytes;
						IPACMDBG_H("Got matched dst-pipe (%d) from %d tx props\n", data->dl_dst_pipe_stats_list[pipe_len].pipe_index, cnt);
						IPACMDBG_H("DL_packets:(%lu) DL_bytes:(%lu) \n", num_dl_packets, num_dl_bytes);
						break;
					}
				}
			}
		}
	}

	if (data->ul_src_pipe_stats_list_valid)
	{
		if(rx_prop != NULL)
		{
			for (pipe_len = 0; pipe_len < data->ul_src_pipe_stats_list_len; pipe_len++)
			{
				IPACMDBG_H("Check entry(%d) dl_dst_pipe(%d)\n", pipe_len, data->ul_src_pipe_stats_list[pipe_len].pipe_index);
				for (cnt=0; cnt < rx_prop->num_rx_props; cnt++)
				{
					IPACMDBG_H("Check Rx_prop_entry(%d) pipe(%d)\n", cnt, ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, rx_prop->rx[cnt].src_pipe));
					if(ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, rx_prop->rx[cnt].src_pipe) == data->ul_src_pipe_stats_list[pipe_len].pipe_index)
					{
						/* update the UL stats */
						ul_pipe_found = true;
						num_ul_packets += data->ul_src_pipe_stats_list[pipe_len].num_ipv4_packets;
						num_ul_packets += data->ul_src_pipe_stats_list[pipe_len].num_ipv6_packets;
						num_ul_bytes += data->ul_src_pipe_stats_list[pipe_len].num_ipv4_bytes;
						num_ul_bytes += data->ul_src_pipe_stats_list[pipe_len].num_ipv6_bytes;
						IPACMDBG_H("Got matched dst-pipe (%d) from %d tx props\n", data->ul_src_pipe_stats_list[pipe_len].pipe_index, cnt);
						IPACMDBG_H("UL_packets:(%lu) UL_bytes:(%lu) \n", num_ul_packets, num_ul_bytes);
						break;
					}
				}
			}
		}
	}
	close(fd);

	if (ul_pipe_found || dl_pipe_found)
	{
		IPACMDBG_H("Update IPA_TETHERING_STATS_UPDATE_EVENT, TX(P%lu/B%lu) RX(P%lu/B%lu) DEV(%s) to LTE(%s) \n",
					num_ul_packets,
						num_ul_bytes,
							num_dl_packets,
								num_dl_bytes,
									dev_name,
										IPACM_Wan::wan_up_dev_name);
		fp = fopen(IPA_PIPE_STATS_FILE_NAME, "w");
		if ( fp == NULL )
		{
			IPACMERR("Failed to write pipe stats to %s, error is %d - %s\n",
					IPA_PIPE_STATS_FILE_NAME, errno, strerror(errno));
			return IPACM_FAILURE;
		}

		fprintf(fp, PIPE_STATS,
				dev_name,
					IPACM_Wan::wan_up_dev_name,
						num_ul_bytes,
						num_ul_packets,
							    num_dl_bytes,
							num_dl_packets);
		fclose(fp);
	}
	return IPACM_SUCCESS;
}

/*handle tether client */
int IPACM_Lan::handle_tethering_client(bool reset, ipacm_client_enum ipa_client)
{
	int cnt, fd, ret = IPACM_SUCCESS;
	int fd_wwan_ioctl = open(WWAN_QMI_IOCTL_DEVICE_NAME, O_RDWR);
	wan_ioctl_set_tether_client_pipe tether_client;

	if(fd_wwan_ioctl < 0)
	{
		IPACMERR("Failed to open %s.\n",WWAN_QMI_IOCTL_DEVICE_NAME);
		return IPACM_FAILURE;
	}

	fd = open(IPA_DEVICE_NAME, O_RDWR);
	if (fd < 0)
	{
		IPACMERR("Failed opening %s.\n", IPA_DEVICE_NAME);
		close(fd_wwan_ioctl);
		return IPACM_FAILURE;
	}

	memset(&tether_client, 0, sizeof(tether_client));
	tether_client.reset_client = reset;
	tether_client.ipa_client = ipa_client;

	if(tx_prop != NULL)
	{
		tether_client.dl_dst_pipe_len = tx_prop->num_tx_props;
		for (cnt = 0; cnt < tx_prop->num_tx_props; cnt++)
		{
			IPACMDBG_H("Tx(%d), dst_pipe: %d, ipa_pipe: %d\n",
					cnt, tx_prop->tx[cnt].dst_pipe,
						ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, tx_prop->tx[cnt].dst_pipe));
			tether_client.dl_dst_pipe_list[cnt] = ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, tx_prop->tx[cnt].dst_pipe);
		}
	}

	if(rx_prop != NULL)
	{
		tether_client.ul_src_pipe_len = rx_prop->num_rx_props;
		for (cnt = 0; cnt < rx_prop->num_rx_props; cnt++)
		{
			IPACMDBG_H("Rx(%d), src_pipe: %d, ipa_pipe: %d\n",
					cnt, rx_prop->rx[cnt].src_pipe,
						ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, rx_prop->rx[cnt].src_pipe));
			tether_client.ul_src_pipe_list[cnt] = ioctl(fd, IPA_IOC_QUERY_EP_MAPPING, rx_prop->rx[cnt].src_pipe);
		}
	}

	ret = ioctl(fd_wwan_ioctl, WAN_IOC_SET_TETHER_CLIENT_PIPE, &tether_client);
	if (ret != 0)
	{
		IPACMERR("Failed set tether-client-pipe %p with ret %d\n ", &tether_client, ret);
	}
	IPACMDBG("Set tether-client-pipe %p\n", &tether_client);
	close(fd);
	close(fd_wwan_ioctl);
	return ret;
}
