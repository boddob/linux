/* Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef MDSS_DSI_PLL_8996_H
#define MDSS_DSI_PLL_8996_H

/* CMN base */
#define DSIPHY_CMN_CLK_CFG0		0x0010
#define DSIPHY_CMN_CLK_CFG1		0x0014
#define DSIPHY_CMN_GLBL_TEST_CTRL	0x0018

#define DSIPHY_CMN_PLL_CNTRL		0x0048
#define DSIPHY_CMN_CTRL_0		0x001c
#define DSIPHY_CMN_CTRL_1		0x0020

#define DSIPHY_CMN_LDO_CNTRL		0x004c

/* PLL base */
#define DSIPHY_PLL_IE_TRIM		0x0000
#define DSIPHY_PLL_IP_TRIM		0x0004

#define DSIPHY_PLL_IPTAT_TRIM		0x0010

#define DSIPHY_PLL_CLKBUFLR_EN		0x001c

#define DSIPHY_PLL_SYSCLK_EN_RESET	0x0028
#define DSIPHY_PLL_RESETSM_CNTRL	0x002c
#define DSIPHY_PLL_RESETSM_CNTRL2	0x0030
#define DSIPHY_PLL_RESETSM_CNTRL3	0x0034
#define DSIPHY_PLL_RESETSM_CNTRL4	0x0038
#define DSIPHY_PLL_RESETSM_CNTRL5	0x003c
#define DSIPHY_PLL_KVCO_DIV_REF1	0x0040
#define DSIPHY_PLL_KVCO_DIV_REF2	0x0044
#define DSIPHY_PLL_KVCO_COUNT1		0x0048
#define DSIPHY_PLL_KVCO_COUNT2		0x004c
#define DSIPHY_PLL_VREF_CFG1		0x005c

#define DSIPHY_PLL_KVCO_CODE		0x0058

#define DSIPHY_PLL_VCO_DIV_REF1		0x006c
#define DSIPHY_PLL_VCO_DIV_REF2		0x0070
#define DSIPHY_PLL_VCO_COUNT1		0x0074
#define DSIPHY_PLL_VCO_COUNT2		0x0078
#define DSIPHY_PLL_PLLLOCK_CMP1		0x007c
#define DSIPHY_PLL_PLLLOCK_CMP2		0x0080
#define DSIPHY_PLL_PLLLOCK_CMP3		0x0084
#define DSIPHY_PLL_PLLLOCK_CMP_EN	0x0088
#define DSIPHY_PLL_PLL_VCO_TUNE		0x008C
#define DSIPHY_PLL_DEC_START		0x0090
#define DSIPHY_PLL_SSC_EN_CENTER	0x0094
#define DSIPHY_PLL_SSC_ADJ_PER1		0x0098
#define DSIPHY_PLL_SSC_ADJ_PER2		0x009c
#define DSIPHY_PLL_SSC_PER1		0x00a0
#define DSIPHY_PLL_SSC_PER2		0x00a4
#define DSIPHY_PLL_SSC_STEP_SIZE1	0x00a8
#define DSIPHY_PLL_SSC_STEP_SIZE2	0x00ac
#define DSIPHY_PLL_DIV_FRAC_START1	0x00b4
#define DSIPHY_PLL_DIV_FRAC_START2	0x00b8
#define DSIPHY_PLL_DIV_FRAC_START3	0x00bc
#define DSIPHY_PLL_TXCLK_EN		0x00c0
#define DSIPHY_PLL_PLL_CRCTRL		0x00c4

#define DSIPHY_PLL_RESET_SM_READY_STATUS 0x00cc

#define DSIPHY_PLL_PLL_MISC1		0x00e8

#define DSIPHY_PLL_CP_SET_CUR		0x00f0
#define DSIPHY_PLL_PLL_ICPMSET		0x00f4
#define DSIPHY_PLL_PLL_ICPCSET		0x00f8
#define DSIPHY_PLL_PLL_ICP_SET		0x00fc
#define DSIPHY_PLL_PLL_LPF1		0x0100
#define DSIPHY_PLL_PLL_LPF2_POSTDIV	0x0104
#define DSIPHY_PLL_PLL_BANDGAP		0x0108

/* CMN base */
#define DSI_DYNAMIC_REFRESH_PLL_CTRL15		0x050
#define DSI_DYNAMIC_REFRESH_PLL_CTRL19		0x060
#define DSI_DYNAMIC_REFRESH_PLL_CTRL20		0x064
#define DSI_DYNAMIC_REFRESH_PLL_CTRL21		0x068
#define DSI_DYNAMIC_REFRESH_PLL_CTRL22		0x06C
#define DSI_DYNAMIC_REFRESH_PLL_CTRL23		0x070
#define DSI_DYNAMIC_REFRESH_PLL_CTRL24		0x074
#define DSI_DYNAMIC_REFRESH_PLL_CTRL25		0x078
#define DSI_DYNAMIC_REFRESH_PLL_CTRL26		0x07C
#define DSI_DYNAMIC_REFRESH_PLL_CTRL27		0x080
#define DSI_DYNAMIC_REFRESH_PLL_CTRL28		0x084
#define DSI_DYNAMIC_REFRESH_PLL_CTRL29		0x088
#define DSI_DYNAMIC_REFRESH_PLL_UPPER_ADDR	0x094
#define DSI_DYNAMIC_REFRESH_PLL_UPPER_ADDR2	0x098

struct dsi_pll_input {
	u32 fref;	/* 19.2 Mhz, reference clk */
	u32 fdata;	/* bit clock rate */
	u32 dsiclk_sel; /* 1, reg: 0x0014 */
	u32 n2div;	/* 1, reg: 0x0010, bit 4-7 */
	u32 ssc_en;	/* 1, reg: 0x0094, bit 0 */
	u32 ldo_en;	/* 0,  reg: 0x004c, bit 0 */

	/* fixed  */
	u32 refclk_dbler_en;	/* 0, reg: 0x00c0, bit 1 */
	u32 vco_measure_time;	/* 5, unknown */
	u32 kvco_measure_time;	/* 5, unknown */
	u32 bandgap_timer;	/* 4, reg: 0x0030, bit 3 - 5 */
	u32 pll_wakeup_timer;	/* 5, reg: 0x003c, bit 0 - 2 */
	u32 plllock_cnt;	/* 1, reg: 0x0088, bit 1 - 2 */
	u32 plllock_rng;	/* 1, reg: 0x0088, bit 3 - 4 */
	u32 ssc_center;		/* 0, reg: 0x0094, bit 1 */
	u32 ssc_adj_period;	/* 37, reg: 0x498, bit 0 - 9 */
	u32 ssc_spread;		/* 0.005  */
	u32 ssc_freq;		/* unknown */
	u32 pll_ie_trim;	/* 4, reg: 0x0000 */
	u32 pll_ip_trim;	/* 4, reg: 0x0004 */
	u32 pll_iptat_trim;	/* reg: 0x0010 */
	u32 pll_cpcset_cur;	/* 1, reg: 0x00f0, bit 0 - 2 */
	u32 pll_cpmset_cur;	/* 1, reg: 0x00f0, bit 3 - 5 */

	u32 pll_icpmset;	/* 4, reg: 0x00fc, bit 3 - 5 */
	u32 pll_icpcset;	/* 4, reg: 0x00fc, bit 0 - 2 */

	u32 pll_icpmset_p;	/* 0, reg: 0x00f4, bit 0 - 2 */
	u32 pll_icpmset_m;	/* 0, reg: 0x00f4, bit 3 - 5 */

	u32 pll_icpcset_p;	/* 0, reg: 0x00f8, bit 0 - 2 */
	u32 pll_icpcset_m;	/* 0, reg: 0x00f8, bit 3 - 5 */

	u32 pll_lpf_res1;	/* 3, reg: 0x0104, bit 0 - 3 */
	u32 pll_lpf_cap1;	/* 11, reg: 0x0100, bit 0 - 3 */
	u32 pll_lpf_cap2;	/* 1, reg: 0x0100, bit 4 - 7 */
	u32 pll_c3ctrl;		/* 2, reg: 0x00c4 */
	u32 pll_r3ctrl;		/* 1, reg: 0x00c4 */
};

struct dsi_pll_output {
	u32 pll_txclk_en;	/* reg: 0x00c0 */
	u32 dec_start;		/* reg: 0x0090 */
	u32 div_frac_start;	/* reg: 0x00b4, 0x00b8, 0x00bc */
	u32 ssc_period;		/* reg: 0x00a0, 0x00a4 */
	u32 ssc_step_size;	/* reg: 0x00a8, 0x00ac */
	u32 plllock_cmp;	/* reg: 0x007c, 0x0080, 0x0084 */
	u32 pll_vco_div_ref;	/* reg: 0x006c, 0x0070 */
	u32 pll_vco_count;	/* reg: 0x0074, 0x0078 */
	u32 pll_kvco_div_ref;	/* reg: 0x0040, 0x0044 */
	u32 pll_kvco_count;	/* reg: 0x0048, 0x004c */
	u32 pll_misc1;		/* reg: 0x00e8 */
	u32 pll_lpf2_postdiv;	/* reg: 0x0104 */
	u32 pll_resetsm_cntrl;	/* reg: 0x002c */
	u32 pll_resetsm_cntrl2;	/* reg: 0x0030 */
	u32 pll_resetsm_cntrl5;	/* reg: 0x003c */
	u32 pll_kvco_code;		/* reg: 0x0058 */

	u32 cmn_clk_cfg0;	/* reg: 0x0010 */
	u32 cmn_clk_cfg1;	/* reg: 0x0014 */
	u32 cmn_ldo_cntrl;	/* reg: 0x004c */

	u32 pll_postdiv;	/* vco */
	u32 pll_n1div;		/* vco */
	u32 pll_n2div;		/* hr_oclk3, pixel */
	u32 fcvo;
};

enum {
	DSI_PLL_0,
	DSI_PLL_1,
	DSI_PLL_NUM
};

struct dsi_pll_db {
	struct dsi_pll_db *next;
	struct mdss_pll_resources *pll;
	struct dsi_pll_input in;
	struct dsi_pll_output out;
	int source_setup_done;
};

enum {
	PLL_OUTPUT_NONE,
	PLL_OUTPUT_RIGHT,
	PLL_OUTPUT_LEFT,
	PLL_OUTPUT_BOTH
};

enum {
	PLL_SOURCE_FROM_LEFT,
	PLL_SOURCE_FROM_RIGHT
};

enum {
	PLL_UNKNOWN,
	PLL_STANDALONE,
	PLL_SLAVE,
	PLL_MASTER
};

#if 0
int pll_vco_set_rate_8996(struct clk *c, unsigned long rate);
long pll_vco_round_rate_8996(struct clk *c, unsigned long rate);
enum handoff pll_vco_handoff_8996(struct clk *c);
enum handoff shadow_pll_vco_handoff_8996(struct clk *c);
int shadow_post_n1_div_set_div(struct div_clk *clk, int div);
int shadow_post_n1_div_get_div(struct div_clk *clk);
int shadow_n2_div_set_div(struct div_clk *clk, int div);
int shadow_n2_div_get_div(struct div_clk *clk);
int shadow_pll_vco_set_rate_8996(struct clk *c, unsigned long rate);
int pll_vco_prepare_8996(struct clk *c);
void pll_vco_unprepare_8996(struct clk *c);
int set_mdss_byte_mux_sel_8996(struct mux_clk *clk, int sel);
int get_mdss_byte_mux_sel_8996(struct mux_clk *clk);
int set_mdss_pixel_mux_sel_8996(struct mux_clk *clk, int sel);
int get_mdss_pixel_mux_sel_8996(struct mux_clk *clk);
int post_n1_div_set_div(struct div_clk *clk, int div);
int post_n1_div_get_div(struct div_clk *clk);
int n2_div_set_div(struct div_clk *clk, int div);
int n2_div_get_div(struct div_clk *clk);
int dsi_pll_enable_seq_8996(struct mdss_pll_resources *pll);
#endif
#endif  /* MDSS_DSI_PLL_8996_H */
