#ifndef __FSM_SIDECAR_XFLOW_H__
#define __FSM_SIDECAR_XFLOW_H__

#include "bpf_macros.h"
#include "bpf_debug.h"
#include "bpf_xtrans.h"

INTERNAL(__u8)
xpkt_trace_check(skb_t *skb, xpkt_t *pkt, cfg_t *cfg)
{
    tr_op_t *op;
    if (cfg->ipv4_trace_by_ip_on) {
        tr_ip_t key;
        XADDR_COPY(key.addr, pkt->flow.saddr);
        op = bpf_map_lookup_elem(&fsm_trip, &key);
        if (op == NULL) {
            XADDR_COPY(key.addr, pkt->flow.daddr);
            op = bpf_map_lookup_elem(&fsm_trip, &key);
        }
        if (op) {
            goto trace_on;
        }
    }
    if (cfg->ipv4_trace_by_port_on) {
        tr_port_t key;
        key.port = pkt->flow.sport;
        op = bpf_map_lookup_elem(&fsm_trpt, &key);
        if (op == NULL) {
            key.port = pkt->flow.dport;
            op = bpf_map_lookup_elem(&fsm_trpt, &key);
        }
        if (op) {
            goto trace_on;
        }
    }
    return 0;
trace_on:
    cfg->ipv4_trace_hdr_on = 1;
    cfg->ipv4_trace_nat_on = 1;
    cfg->ipv4_trace_opt_on = 1;
    cfg->ipv4_trace_acl_on = 1;
    cfg->ipv4_trace_flow_on = 1;
    return 1;
}

INTERNAL(__u8)
xpkt_acl_check(skb_t *skb, xpkt_t *pkt, cfg_t *cfg)
{
    acl_key_t key;
    acl_op_t *op;

    key.proto = pkt->flow.proto;
    if (pkt->tc_dir == TC_DIR_IGR) {
        XADDR_COPY(key.addr, pkt->flow.saddr);
        key.port = pkt->flow.sport;
    } else if (pkt->tc_dir == TC_DIR_EGR) {
        XADDR_COPY(key.addr, pkt->flow.daddr);
        key.port = pkt->flow.dport;
    } else {
        if (cfg->ipv4_trace_acl_on || cfg->ipv4_trace_nat_on) {
            FSM_DBG("[DBG] ACL DENY\n");
        }
        xpkt_tail_call(skb, pkt, FSM_CNI_DROP_PROG_ID);
        return ACL_DENY;
    }

    op = bpf_map_lookup_elem(&fsm_xacl, &key);
    if (op != NULL && op->acl == ACL_AUDIT) {
        return ACL_AUDIT;
    }

    if (op == NULL) {
        key.port = 0;
        op = bpf_map_lookup_elem(&fsm_xacl, &key);
        if (op == NULL) {
            return ACL_AUDIT;
        }
    }

    if (op->acl > ACL_AUDIT) {
        if (cfg->ipv4_trace_acl_on || cfg->ipv4_trace_nat_on) {
            FSM_DBG("[DBG] ACL TRUSTED\n");
        }
        xpkt_tail_call(skb, pkt, FSM_CNI_PASS_PROG_ID);
        return ACL_TRUSTED;
    } else if (op->acl < ACL_AUDIT) {
        if (cfg->ipv4_trace_acl_on || cfg->ipv4_trace_nat_on) {
            FSM_DBG("[DBG] ACL DENY\n");
        }
        xpkt_tail_call(skb, pkt, FSM_CNI_DROP_PROG_ID);
        return ACL_DENY;
    }

    return ACL_AUDIT;
}

INTERNAL(int)
xpkt_flow_nat_endpoint(skb_t *skb, xpkt_t *pkt, nat_op_t *ops)
{
    int sel = -1;
    __u16 ep_idx = 0, ep_sel = 0;
    nat_ep_t *ep;

    xpkt_spin_lock(&ops->lock);
    ep_sel = ops->ep_sel;
    while (ep_idx < FSM_NAT_MAX_ENDPOINTS) {
        if (ep_sel < FSM_NAT_MAX_ENDPOINTS) {
            ep = &ops->eps[ep_sel];
            if (ep->inactive == 0) {
                ep_sel = (ep_sel + 1) % ops->ep_cnt;
                ops->ep_sel = ep_sel;
                sel = ep_sel;
                break;
            }
        }
        ep_sel++;
        ep_sel = ep_sel % ops->ep_cnt;
        ep_idx++;
    }
    xpkt_spin_unlock(&ops->lock);
    return sel;
}

INTERNAL(int)
xpkt_flow_nat(skb_t *skb, xpkt_t *pkt, xnat_t *xnat, __u8 with_addr,
              __u8 with_port)
{
    nat_key_t key;
    nat_op_t *ops;
    nat_ep_t *ep;
    int ep_sel;

    if (with_addr) {
        XADDR_COPY(key.daddr, pkt->flow.daddr);
    } else {
        XADDR_ZERO(key.daddr);
    }

    if (with_port) {
        key.dport = pkt->flow.dport;
    } else {
        key.dport = 0;
    }
    key.proto = pkt->flow.proto;
    key.tc_dir = pkt->tc_dir;
    key.v6 = pkt->v6;

    ops = bpf_map_lookup_elem(&fsm_xnat, &key);
    if (!ops) {
        return 0;
    }

    ep_sel = xpkt_flow_nat_endpoint(skb, pkt, ops);
    if (ep_sel >= 0 && ep_sel < FSM_NAT_MAX_ENDPOINTS) {
        ep = &ops->eps[ep_sel];
        XMAC_COPY(xnat->rmac, ep->rmac);
        XADDR_COPY(xnat->raddr, ep->raddr);
        xnat->rport = ep->rport;
        return 1;
    }

    return 0;
}

INTERNAL(int)
xpkt_flow_proc_frag(xpkt_t *pkt, void *fsm_xflow, flow_t *cflow, flow_t *rflow,
                    flow_op_t *cop, flow_op_t *rop)
{
    flow_op_t *ucop, *urop;
    int cidx = 0, ridx = 1;

    ucop = bpf_map_lookup_elem(&fsm_cflop, &cidx);
    urop = bpf_map_lookup_elem(&fsm_cflop, &ridx);
    if (ucop == NULL || urop == NULL || pkt->v6) {
        return 0;
    }

    XFLOW_OP_COPY(ucop, cop);
    XFLOW_OP_COPY(urop, rop);

    if (pkt->flow_dir == FLOW_DIR_C2S) {
        cflow->sport = pkt->ipv4_id;
        cflow->dport = pkt->ipv4_id;
        bpf_map_update_elem(fsm_xflow, cflow, ucop, BPF_ANY);
    } else {
        rflow->sport = pkt->ipv4_id;
        rflow->dport = pkt->ipv4_id;
        bpf_map_update_elem(fsm_xflow, rflow, urop, BPF_ANY);
    }
    return 0;
}

INTERNAL(int)
xpkt_flow_init_reverse_op(xpkt_t *pkt, cfg_t *cfg, void *fsm_xflow,
                          flow_t *flow, flow_op_t *op)
{
    flow_t rflow;
    flow_op_t *rop;
    int ridx = 1;

    XADDR_COPY(&rflow.daddr, op->xnat.xaddr);
    XADDR_COPY(&rflow.saddr, op->xnat.raddr);
    rflow.dport = op->xnat.xport;
    rflow.sport = op->xnat.rport;
    rflow.proto = flow->proto;
    rflow.v6 = flow->v6;

    rop = bpf_map_lookup_elem(&fsm_cflop, &ridx);
    if (rop == NULL) {
        return 0;
    }

    memset(rop, 0, sizeof(flow_op_t));

    if (pkt->tc_dir == TC_DIR_EGR) {
        rop->flow_dir = FLOW_DIR_S2C;
        XFUNC_EXCH(rop->nfs, op->nfs);
    }
    if (pkt->tc_dir == TC_DIR_IGR) {
        rop->flow_dir = FLOW_DIR_S2C;
        XFUNC_COPY(rop->nfs, op->nfs);
    }

    XMAC_COPY(rop->xnat.xmac, pkt->dmac);
    XMAC_COPY(rop->xnat.rmac, pkt->smac);
    XADDR_COPY(rop->xnat.xaddr, flow->daddr);
    XADDR_COPY(rop->xnat.raddr, flow->saddr);
    rop->xnat.xport = flow->dport;
    rop->xnat.rport = flow->sport;
    rop->do_trans = 1;

    bpf_map_update_elem(fsm_xflow, &rflow, rop, BPF_ANY);
    if (cfg->ipv4_trace_flow_on) {
        FSM_DBG_FLOW("INSERT FLOW-R:", &rflow);
    }

    return 1;
}

INTERNAL(int)
xpkt_flow_init_ops(skb_t *skb, xpkt_t *pkt, cfg_t *cfg, void *fsm_xflow,
                   void *fsm_xopt)
{
    flow_t *flow;
    flow_op_t *op;
    int idx = 0;
    int do_nat = 0;

    if (cfg->ipv4_trace_flow_on) {
        FSM_DBG("[DBG] FLOW INIT\n");
    }

    flow = &pkt->flow;
    op = bpf_map_lookup_elem(&fsm_cflop, &idx);
    if (op == NULL) {
        return 0;
    }
    memset(op, 0, sizeof(flow_op_t));

    if (pkt->tc_dir == TC_DIR_EGR) {
        op->flow_dir = FLOW_DIR_C2S;
        op->do_trans = 1;
        op->nfs[TC_DIR_IGR] = NF_DENY;
        op->nfs[TC_DIR_EGR] = NF_XNAT | NF_ALLOW;
        XMAC_COPY(op->xnat.xmac, pkt->smac);
        XADDR_COPY(op->xnat.xaddr, flow->saddr);
        op->xnat.xport = flow->sport;
    } else if (pkt->tc_dir == TC_DIR_IGR) {
        op->flow_dir = FLOW_DIR_C2S;
        op->do_trans = 1;
        op->nfs[TC_DIR_IGR] = NF_RDIR | NF_SKIP_SM;
        op->nfs[TC_DIR_EGR] = NF_XNAT | NF_ALLOW;
        XMAC_COPY(op->xnat.xmac, pkt->dmac);
        XADDR_COPY(op->xnat.xaddr, flow->daddr);
        op->xnat.xport = flow->sport;
    }

    if (pkt->flow.proto == IPPROTO_TCP) {
        if (cfg->ipv4_tcp_nat_by_ip_port_on) {
            do_nat = xpkt_flow_nat(skb, pkt, &op->xnat, 1, 1);
        }
        if (!do_nat && cfg->ipv4_tcp_nat_by_ip_on) {
            do_nat = xpkt_flow_nat(skb, pkt, &op->xnat, 1, 0);
        }
        if (!do_nat && !cfg->ipv4_tcp_nat_all_off) {
            do_nat = xpkt_flow_nat(skb, pkt, &op->xnat, 0, 0);
        }

        if (!do_nat) {
            if (!xpkt_flow_nat(skb, pkt, &op->xnat, 0, 0)) {
                pkt->nfs[TC_DIR_IGR] = NF_DENY;
                pkt->nfs[TC_DIR_EGR] = NF_DENY;
                if (cfg->ipv4_trace_nat_on) {
                    FSM_DBG("[DBG] DROP BY NO NAT\n");
                }
                xpkt_tail_call(skb, pkt, FSM_CNI_DROP_PROG_ID);
                return 0;
            }
        }
    } else if (pkt->flow.proto == IPPROTO_UDP) {
        if (cfg->ipv4_udp_nat_by_ip_port_on) {
            do_nat = xpkt_flow_nat(skb, pkt, &op->xnat, 1, 1);
        }
        if (!do_nat && cfg->ipv4_udp_nat_by_ip_on) {
            do_nat = xpkt_flow_nat(skb, pkt, &op->xnat, 1, 0);
        }
        if (!do_nat && cfg->ipv4_udp_nat_by_port_on) {
            do_nat = xpkt_flow_nat(skb, pkt, &op->xnat, 0, 1);
        }
        if (!do_nat && !cfg->ipv4_udp_nat_all_off) {
            do_nat = xpkt_flow_nat(skb, pkt, &op->xnat, 0, 0);
        }

        if (!do_nat) {
            if (!xpkt_flow_nat(skb, pkt, &op->xnat, 0, 0)) {
                pkt->nfs[TC_DIR_IGR] = NF_DENY;
                pkt->nfs[TC_DIR_EGR] = NF_DENY;
                if (cfg->ipv4_trace_nat_on) {
                    FSM_DBG("[DBG] DROP BY NO NAT\n");
                }
                xpkt_tail_call(skb, pkt, FSM_CNI_DROP_PROG_ID);
                return 0;
            }
        }
    }

    if (cfg->ipv4_tcp_nat_opt_on && pkt->flow.proto == IPPROTO_TCP) {
        if (XFLAG_HAS(op->nfs[TC_DIR_EGR], NF_XNAT)) {
            opt_key_t opt;
            XADDR_COPY(opt.raddr, op->xnat.xaddr);
            if (cfg->ipv4_tcp_nat_opt_with_local_addr_on) {
                XADDR_COPY(opt.laddr, op->xnat.raddr);
            } else {
                XADDR_ZERO(opt.laddr);
            }

            opt.rport = op->xnat.xport;
            if (cfg->ipv4_tcp_nat_opt_with_local_port_on) {
                opt.lport = op->xnat.rport;
            } else {
                opt.lport = 0;
            }
            opt.proto = flow->proto;
            opt.v6 = flow->v6;
            bpf_map_update_elem(fsm_xopt, &opt, flow, BPF_ANY);
            if (cfg->ipv4_trace_opt_on) {
                FSM_DBG_NAT_OPT("INSERT XNAT OPT:", &opt, flow);
            }
        }
    } else if (cfg->ipv4_udp_nat_opt_on && pkt->flow.proto == IPPROTO_UDP) {
        if (XFLAG_HAS(op->nfs[TC_DIR_EGR], NF_XNAT)) {
            opt_key_t opt;
            XADDR_COPY(opt.raddr, op->xnat.xaddr);
            if (cfg->ipv4_tcp_nat_opt_with_local_addr_on) {
                XADDR_COPY(opt.laddr, op->xnat.raddr);
            } else {
                XADDR_ZERO(opt.laddr);
            }

            opt.rport = op->xnat.xport;
            if (cfg->ipv4_tcp_nat_opt_with_local_port_on) {
                opt.lport = op->xnat.rport;
            } else {
                opt.lport = 0;
            }
            opt.proto = flow->proto;
            opt.v6 = flow->v6;
            bpf_map_update_elem(fsm_xopt, &opt, flow, BPF_ANY);
            if (cfg->ipv4_trace_opt_on) {
                FSM_DBG_NAT_OPT("INSERT XNAT OPT:", &opt, flow);
            }
        }
    }

    bpf_map_update_elem(fsm_xflow, flow, op, BPF_ANY);
    if (cfg->ipv4_trace_flow_on) {
        FSM_DBG_FLOW("INSERT FLOW:", flow);
    }

    return xpkt_flow_init_reverse_op(pkt, cfg, fsm_xflow, flow, op);
}

INTERNAL(__s8)
xpkt_flow_proc(skb_t *skb, xpkt_t *pkt, cfg_t *cfg, void *fsm_xflow,
               void *fsm_xopt)
{
    if (fsm_xflow == NULL || fsm_xopt == NULL) {
        return TRANS_ERR;
    }
    flow_t flow, rflow;
    flow_op_t *op, *rop;
    opt_key_t opt;
    __s8 trans = TRANS_ERR;

    XFLOW_COPY(&flow, &pkt->flow);
    if (cfg->ipv4_trace_flow_on) {
        FSM_DBG_FLOW("FOUND FLOW:", &flow);
    }

    op = bpf_map_lookup_elem(fsm_xflow, &flow);
    if (op == NULL) {
        if (!xpkt_flow_init_ops(skb, pkt, cfg, fsm_xflow, fsm_xopt)) {
            return trans;
        }
        op = bpf_map_lookup_elem(fsm_xflow, &flow);
    } else {
        if (pkt->l4_fin) {
            op->fin = 1;
        }

        if (op->fin || pkt->re_flow || op->do_trans) {
            goto flow_track;
        }

        XFUNC_COPY(pkt->nfs, op->nfs);
        XMAC_COPY(pkt->xmac, op->xnat.xmac);
        XMAC_COPY(pkt->rmac, op->xnat.rmac);
        XADDR_COPY(pkt->xaddr, op->xnat.xaddr);
        XADDR_COPY(pkt->raddr, op->xnat.raddr);
        pkt->xport = op->xnat.xport;
        pkt->rport = op->xnat.rport;

        if (XFLAG_HAS(op->nfs[pkt->tc_dir], NF_SKIP_SM)) {
            return TRANS_EST;
        }

        if (pkt->flow.proto == IPPROTO_TCP) {
            op->trans.tcp.conns[FLOW_DIR_C2S].prev_seq = pkt->tcp_seq;
            op->trans.tcp.conns[FLOW_DIR_C2S].prev_ack_seq = pkt->tcp_ack_seq;
        } else if (pkt->flow.proto == IPPROTO_UDP) {
            op->trans.udp.conns.pkts++;
        }

        op->atime = bpf_ktime_get_ns();

        return TRANS_EST;
    }

flow_track:
    if (op != NULL) {
        XFUNC_COPY(pkt->nfs, op->nfs);
        XMAC_COPY(pkt->xmac, op->xnat.xmac);
        XMAC_COPY(pkt->rmac, op->xnat.rmac);
        XADDR_COPY(pkt->xaddr, op->xnat.xaddr);
        XADDR_COPY(pkt->raddr, op->xnat.raddr);
        pkt->xport = op->xnat.xport;
        pkt->rport = op->xnat.rport;

        if (XFLAG_HAS(op->nfs[pkt->tc_dir], NF_SKIP_SM)) {
            return TRANS_EST;
        }

        XADDR_COPY(&rflow.daddr, op->xnat.xaddr);
        XADDR_COPY(&rflow.saddr, op->xnat.raddr);
        rflow.dport = op->xnat.xport;
        rflow.sport = op->xnat.rport;
        rflow.proto = flow.proto;
        rflow.v6 = flow.v6;

        if (cfg->ipv4_trace_flow_on) {
            FSM_DBG_FLOW("FOUND FLOW-R:", &rflow);
        }

        rop = bpf_map_lookup_elem(fsm_xflow, &rflow);
    }

    if (op != NULL && rop != NULL) {
        op->atime = bpf_ktime_get_ns();
        rop->atime = op->atime;
        if (op->flow_dir == FLOW_DIR_C2S) {
            trans = xpkt_trans_proc(skb, pkt, op, rop, FLOW_DIR_C2S);
        } else {
            trans = xpkt_trans_proc(skb, pkt, rop, op, FLOW_DIR_S2C);
        }
        if (cfg->ipv4_trace_flow_on) {
            FSM_DBG("[DBG] TRANS TO: %d\n", trans);
        }

        if (trans == TRANS_EST) {
            op->do_trans = 0;
            rop->do_trans = 0;
            if (pkt->ipv4_id && pkt->flow.proto == IPPROTO_UDP) {
                if (op->flow_dir == FLOW_DIR_C2S) {
                    xpkt_flow_proc_frag(pkt, fsm_xflow, &flow, &rflow, op, rop);
                } else {
                    xpkt_flow_proc_frag(pkt, fsm_xflow, &rflow, &flow, rop, op);
                }
            }
        } else if (trans == TRANS_ERR || trans == TRANS_CWT) {
            if (cfg->ipv4_tcp_nat_opt_on && pkt->flow.proto == IPPROTO_TCP) {
                if (XFLAG_HAS(rop->nfs[TC_DIR_EGR], NF_XNAT)) {
                    XADDR_COPY(opt.raddr, rop->xnat.xaddr);
                    if (cfg->ipv4_tcp_nat_opt_with_local_addr_on) {
                        XADDR_COPY(opt.laddr, rop->xnat.raddr);
                    } else {
                        XADDR_ZERO(opt.laddr);
                    }
                    opt.rport = rop->xnat.xport;
                    if (cfg->ipv4_tcp_nat_opt_with_local_port_on) {
                        opt.lport = rop->xnat.rport;
                    } else {
                        opt.lport = 0;
                    }
                    opt.proto = rflow.proto;
                    opt.v6 = rflow.v6;
                    bpf_map_delete_elem(fsm_xopt, &opt);
                    if (cfg->ipv4_trace_opt_on) {
                        FSM_DBG_NAT_OPT("DELETE XNAT OPT:", &opt, &rflow);
                    }
                }
                if (XFLAG_HAS(op->nfs[TC_DIR_EGR], NF_XNAT)) {
                    XADDR_COPY(opt.raddr, op->xnat.xaddr);
                    if (cfg->ipv4_tcp_nat_opt_with_local_addr_on) {
                        XADDR_COPY(opt.laddr, op->xnat.raddr);
                    } else {
                        XADDR_ZERO(opt.laddr);
                    }
                    opt.rport = op->xnat.xport;
                    if (cfg->ipv4_tcp_nat_opt_with_local_port_on) {
                        opt.lport = op->xnat.rport;
                    } else {
                        opt.lport = 0;
                    }
                    opt.proto = flow.proto;
                    opt.v6 = flow.v6;
                    bpf_map_delete_elem(fsm_xopt, &opt);
                    if (cfg->ipv4_trace_opt_on) {
                        FSM_DBG_NAT_OPT("DELETE XNAT OPT:", &opt, &flow);
                    }
                }
            } else if (cfg->ipv4_udp_nat_opt_on &&
                       pkt->flow.proto == IPPROTO_UDP) {
                if (XFLAG_HAS(rop->nfs[TC_DIR_EGR], NF_XNAT)) {
                    XADDR_COPY(opt.raddr, rop->xnat.xaddr);
                    if (cfg->ipv4_tcp_nat_opt_with_local_addr_on) {
                        XADDR_COPY(opt.laddr, rop->xnat.raddr);
                    } else {
                        XADDR_ZERO(opt.laddr);
                    }
                    opt.rport = rop->xnat.xport;
                    if (cfg->ipv4_tcp_nat_opt_with_local_port_on) {
                        opt.lport = rop->xnat.rport;
                    } else {
                        opt.lport = 0;
                    }
                    opt.proto = rflow.proto;
                    opt.v6 = rflow.v6;
                    bpf_map_delete_elem(fsm_xopt, &opt);
                    if (cfg->ipv4_trace_opt_on) {
                        FSM_DBG_NAT_OPT("DELETE XNAT OPT:", &opt, &rflow);
                    }
                }
                if (XFLAG_HAS(op->nfs[TC_DIR_EGR], NF_XNAT)) {
                    XADDR_COPY(opt.raddr, op->xnat.xaddr);
                    if (cfg->ipv4_tcp_nat_opt_with_local_addr_on) {
                        XADDR_COPY(opt.laddr, op->xnat.raddr);
                    } else {
                        XADDR_ZERO(opt.laddr);
                    }
                    opt.rport = op->xnat.xport;
                    if (cfg->ipv4_tcp_nat_opt_with_local_port_on) {
                        opt.lport = op->xnat.rport;
                    } else {
                        opt.lport = 0;
                    }
                    opt.proto = flow.proto;
                    opt.v6 = flow.v6;
                    bpf_map_delete_elem(fsm_xopt, &opt);
                    if (cfg->ipv4_trace_opt_on) {
                        FSM_DBG_NAT_OPT("DELETE XNAT OPT:", &opt, &flow);
                    }
                }
            }
            bpf_map_delete_elem(fsm_xflow, &rflow);
            bpf_map_delete_elem(fsm_xflow, &flow);
            if (cfg->ipv4_trace_flow_on) {
                FSM_DBG_FLOW("DELETE FLOW-R:", &rflow);
                FSM_DBG_FLOW("DELETE FLOW:", &flow);
                FSM_DBG("[DBG] DELETE FLOWS\n");
            }
        }
    }

    return trans;
}

#endif