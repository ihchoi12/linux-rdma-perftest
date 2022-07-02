/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perftest_parameters.h"
#include "perftest_resources.h"
#include "multicast_resources.h"
#include "perftest_communication.h"
#include "message.h"

/******************************************************************************
 *
 ******************************************************************************/
static int set_mcast_group(struct pingpong_context *ctx,
		struct perftest_parameters *user_param,
		struct mcast_parameters *mcg_params)
{
	struct ibv_port_attr port_attr;

	if (ibv_query_gid(ctx->context,user_param->ib_port,user_param->gid_index,&mcg_params->port_gid)) {
		return FAILURE;
	}

	if (ibv_query_pkey(ctx->context,user_param->ib_port,DEF_PKEY_IDX,&mcg_params->pkey)) {
		return FAILURE;
	}

	if (ibv_query_port(ctx->context,user_param->ib_port,&port_attr)) {
		return FAILURE;
	}
	mcg_params->sm_lid  = port_attr.sm_lid;
	mcg_params->sm_sl   = port_attr.sm_sl;
	mcg_params->ib_port = user_param->ib_port;
	mcg_params->ib_ctx  = ctx->context;
	mcg_params->ib_devname = user_param->ib_devname;

	if (!strcmp(link_layer_str(user_param->link_type),"IB")) {
		/* Request for Mcast group create registery in SM. */
		if (join_multicast_group(SUBN_ADM_METHOD_SET,mcg_params)) {
			fprintf(stderr,"Couldn't Register the Mcast group on the SM\n");
			return FAILURE;
		}
	}
	return 0;
}

/******************************************************************************
 *
 ******************************************************************************/
static int send_set_up_connection(struct pingpong_context *ctx,
		struct perftest_parameters *user_param,
		struct pingpong_dest *my_dest,
		struct mcast_parameters *mcg_params,
		struct perftest_comm *comm)
{
	int i;

	if (set_up_connection(ctx,user_param,my_dest)) {
		fprintf(stderr," Unable to set up my IB connection parameters\n");
		return FAILURE;
	}

	if (user_param->use_mcg && (user_param->duplex || user_param->machine == SERVER)) {

		mcg_params->user_mgid = user_param->user_mgid;
		set_multicast_gid(mcg_params,ctx->qp[0]->qp_num,(int)user_param->machine);
		if (set_mcast_group(ctx,user_param,mcg_params)) {
			return FAILURE;
		}

		for (i=0; i < user_param->num_of_qps; i++) {
			if (ibv_attach_mcast(ctx->qp[i],&mcg_params->mgid,mcg_params->mlid)) {
				fprintf(stderr, "Couldn't attach QP to MultiCast group");
				return FAILURE;
			}
		}

		mcg_params->mcast_state |= MCAST_IS_ATTACHED;
		my_dest->gid = mcg_params->mgid;
		my_dest->lid = mcg_params->mlid;
		my_dest->qpn = QPNUM_MCAST;
	}
	return 0;
}

/******************************************************************************
 *
 ******************************************************************************/
static int send_destroy_ctx(
		struct pingpong_context *ctx,
		struct perftest_parameters *user_param,
		struct mcast_parameters *mcg_params)
{
	int i;
	if (user_param->use_mcg) {

		if (user_param->duplex || user_param->machine == SERVER) {
			for (i=0; i < user_param->num_of_qps; i++) {
				if (ibv_detach_mcast(ctx->qp[i],&mcg_params->mgid,mcg_params->mlid)) {
					fprintf(stderr, "Couldn't attach QP to MultiCast group");
					return FAILURE;
				}
			}
		}

		/* Removal Request for Mcast group in SM if needed. */
		if (!strcmp(link_layer_str(user_param->link_type),"IB")) {
			if (join_multicast_group(SUBN_ADM_METHOD_DELETE,mcg_params)) {
				fprintf(stderr,"Couldn't Unregister the Mcast group on the SM\n");
				return FAILURE;
			}
		}
	}
	return destroy_ctx(ctx,user_param);
}

/******************************************************************************
 *
 ******************************************************************************/
int main(int argc, char *argv[])
{
	struct ibv_device		*ib_dev = NULL;
	struct pingpong_context  	ctx;
	struct pingpong_dest	 	*my_dest  = NULL;
	struct pingpong_dest		*rem_dest = NULL;
	struct perftest_parameters  	user_param;
	struct perftest_comm		user_comm;
	struct mcast_parameters     	mcg_params;
	struct bw_report_data		my_bw_rep, rem_bw_rep;
	int                      	ret_parser, i = 0, rc;
	int                      	size_max_pow = 24;

	fprintf(stderr, " Hi Learning\n");
	/* init default values to user's parameters */
	memset(&ctx, 0,sizeof(struct pingpong_context));
	memset(&user_param, 0 , sizeof(struct perftest_parameters));
	memset(&mcg_params, 0 , sizeof(struct mcast_parameters));
	memset(&user_comm, 0,sizeof(struct perftest_comm));

	user_param.verb    = SEND;
	user_param.tst     = BW;
	strncpy(user_param.version, VERSION, sizeof(user_param.version));

	/* Configure the parameters values according to user arguments or defalut values. */
	ret_parser = parser(&user_param,argv,argc);
	if (ret_parser) {
		if (ret_parser != VERSION_EXIT && ret_parser != HELP_EXIT)
			fprintf(stderr," Parser function exited with Error\n");
		return FAILURE;
	}
	if((user_param.connection_type == DC || user_param.use_xrc) && user_param.duplex) {
		user_param.num_of_qps *= 2;
	}
	/* Checking that the user did not run with RawEth. for this we have raw_etherent_bw test. */
	if (user_param.connection_type == RawEth) {
		fprintf(stderr," This test cannot run Raw Ethernet QPs (you have chosen RawEth as connection type\n");
		fprintf(stderr," For this we have raw_ethernet_bw test in this package.\n");
		return FAILURE;
	}

	/* Finding the IB device selected (or defalut if no selected). */
	ib_dev = ctx_find_dev(&user_param.ib_devname);
	if (!ib_dev) {
		fprintf(stderr," Unable to find the Infiniband/RoCE device\n");
		return FAILURE;
	}
	fprintf(stderr, " ib_dev name: %s\n", ib_dev->name);
	Debug("DEBUG");

	return 0;
}
