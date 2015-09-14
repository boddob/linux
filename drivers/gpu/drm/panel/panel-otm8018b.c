/*
 * Copyright (C) 2015 InforceComputing
 * Author: Vinay Simha BN <vinaysimha@inforcecomputing.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct otm8018b_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct backlight_device *backlight;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;

	bool prepared;
	bool enabled;

	const struct drm_display_mode *mode;
};

static inline struct otm8018b_panel *to_otm8018b_panel(struct drm_panel *panel)
{
	return container_of(panel, struct otm8018b_panel, base);
}

/*************************** IC for AUO 4' MIPI * 480RGBx854************/
static char write_memory1[4]={0xFF,0x80,0x09,0x01};//Enable EXTC
static char write_memory2[2]={0x00,0x80};//Shift address
static char write_memory3[3]={0xFF,0x80,0x09};	 //Enable Orise mode

static char write_memory4[2]={0x00,0x00};
static char write_memory5[2]={0xD8,0x87};//GVDD	4.8V
static char write_memory6[2]={0x00,0x01};
static char write_memory7[2]={0xD8,0x87};//NGVDD -4.8V
static char write_memory8[2]={0x00,0xB1};
static char write_memory9[2]={0xC5,0xA9};//[0]GVDD output Enable : 0xA9	VDD_18V=LVDSVDD=1.8V
static char write_memory10[2]={0x00,0x91};
static char write_memory11[2]={0xC5,0x79};//[7:4]VGH_level=15V [3:0]VGL_level=-12V
static char write_memory12[2]={0x00,0x00};
static char write_memory13[2]={0xD9,0x45};//VCOM=-1.15V

static char write_memory14[2]={0x00,0x92};
static char write_memory15[2]={0xC5,0x01};//pump45

static char write_memory16[2]={0x00,0xA1};
static char write_memory17[2]={0xC1,0x08};//reg_oscref_rgb_vs_video

static char write_memory18[2]={0x00,0x81};
static char write_memory19[2]={0xC1,0x66};//OSC Adj=65Hz

static char write_memory20[2]={0x00,0xa3};
static char write_memory21[2]={0xc0,0x1B};//source pch

static char write_memory22[2]={0x00,0x82};
static char write_memory23[2]={0xc5,0x83};//REG-Pump23 AVEE VCL

static char write_memory24[2]={0x00,0x81};
static char write_memory25[2]={0xc4,0x83};			 //source bias

static char write_memory26[2]={0x00,0x90};
static char write_memory27[2]={0xB3,0x02};			 //SW_GM 480X854

static char write_memory29[2]={0x00,0x92};
static char write_memory30[2]={0xB3,0x45};			 //Enable SW_GM

static char write_memory31[2]={0x00,0xa0};
static char write_memory32[2]={0xc1,0xea};

static char write_memory33[2]={0x00,0xc0};
static char write_memory34[2]={0xc5,0x00};

static char write_memory35[2]={0x00,0x8b};
static char write_memory36[2]={0xb0,0x40};

static char write_memory37[2]={0x00,0x87};
static char write_memory38[4]={0xC4,0x00,0x80,0x00};

static char write_memory39[2]={0x00,0xB2};
static char write_memory40[5]={0xF5,0x15,0x00,0x15,0x00}; //VRGH Disable

static char write_memory41[2]={0x00,0x93};
static char write_memory42[2]={0xC5,0x03}; //VRGH minimum
///////////////////////////////////////////////////////////////////////////////
static char write_memory43[2]={0x00,0xa7};
static char write_memory44[2]={0xb3,0x01};          //panel_set[0]	= 1

static char write_memory45[2]={0x00,0xa6};
static char write_memory46[2]={0xb3,0x2b};          //reg_panel_zinv,reg_panel_zinv_pixel,reg_panel_zinv_odd,reg_panel_zigzag,reg_panel_zigzag_blue,reg_panel_zigzag_shift_r,reg_panel_zigzag_odd

//C09x : mck_shift1/mck_shift2/mck_shift3
static char write_memory47[2]={0x00,0x90};
static char write_memory48[7]={0xC0,0x00,0x4E,0x00,0x00,0x00,0x03};

//C0Ax : hs_shift/vs_shift

static char write_memory49[2]={0x00,0xa6};
static char write_memory50[4]={0xC1,0x01,0x00,0x00}; // 16'hc1a7 [7:0] : oscref_vedio_hs_shift[7:0]

//Gamma2.2 +/-
static char write_memory51[2]={0x00,0x00};
static char write_memory52[17]={0xE1,0x05,0x0B,0x0F,0x0F,0x08,0x0D,0x0C,0x0B,0x02,0x06,0x16,0x12,0x18,0x24,0x17,0x00};
//V255 V251 V247 V239 V231 V203 V175 V147 V108 V80  V52	 V24  V16  V8   V4   V0

static char write_memory53[2]={0x00,0x00};
static char write_memory54[17]={0xE2,0x05,0x0B,0x0F,0x0F,0x08,0x0D,0x0C,0x0B,0x02,0x06,0x16,0x12,0x18,0x24,0x17,0x00};
//V255 V251 V247 V239 V231 V203 V175 V147 V108 V80  V52	 V24  V16  V8   V4   V0

//--------------------------------------------------------------------------------
//		initial setting 2 < tcon_goa_wave >
//--------------------------------------------------------------------------------
static char write_memory55[2]={0x00,0x91};         //zigzag reverse scan
static char write_memory56[2]={0xB3,0x00};

//CE8x : vst1, vst2, vst3, vst4
static char write_memory57[2]={0x00, 0x80};
static char write_memory58[13]={0xCE,0x85,0x01,0x18,0x84,0x01,0x18,0x00,0x00,0x00,0x00,0x00,0x00};

//CE9x : vend1, vend2, vend3, vend4
static char write_memory59[2]={0x00, 0x90};
static char write_memory60[15]={0xCE,0x13,0x56,0x18,0x13,0x57,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

//CEAx : clka1, clka2
static char write_memory61[2]={0x00, 0xa0};
static char write_memory62[15]={0xCE,0x18,0x0B,0x03,0x5E,0x00,0x18,0x00,0x18,0x0A,0x03,0x5F,0x00,0x18,0x00};

//CEBx : clka3, clka4
static char write_memory63[2]={0x00, 0xb0};
static char write_memory64[15]={0xCE,0x18,0x0D,0x03,0x5C,0x00,0x18,0x00,0x18,0x0C,0x03,0x5D,0x00,0x18,0x00};

//CECx : clkb1, clkb2
static char write_memory65[2]={0x00, 0xc0};
static char write_memory66[15]={0xCE,0x38,0x0D,0x03,0x5E,0x00,0x10,0x07,0x38,0x0C,0x03,0x5F,0x00,0x10,0x07};

//CEDx : clkb3, clkb4
static char write_memory67[2]={0x00, 0xd0};
static char write_memory68[15]={0xCE,0x38,0x09,0x03,0x5A,0x00,0x10,0x07,0x38,0x08,0x03,0x5B,0x00,0x10,0x07};

//CFCx :
static char write_memory69[2]={0x00, 0xC7};
static char write_memory70[2]={0xCF, 0x04};

static char write_memory71[2]={0x00, 0xC9};
static char write_memory72[2]={0xCF, 0x00};

//--------------------------------------------------------------------------------
//		initial setting 3 < Panel setting >
//--------------------------------------------------------------------------------
// cbcx
static char write_memory73[2]={0x00, 0xC0};
static char write_memory74[2]={0xCB, 0x14};

static char write_memory75[2]={0x00, 0xC2};
static char write_memory76[6]={0xCB, 0x14,0x14,0x14,0x14,0x14};

// cbdx
static char write_memory77[2]={0x00, 0xD5};
static char write_memory78[2]={0xCB, 0x14};

static char write_memory79[2]={0x00, 0xD7};
static char write_memory80[6]={0xCB, 0x14,0x14,0x14,0x14,0x14};

// cc8x
static char write_memory81[2]={0x00, 0x80};
static char write_memory82[2]={0xCC, 0x01};

static char write_memory83[2]={0x00, 0x82};
static char write_memory84[6]={0xCC, 0x0F,0x0D,0x0B,0x09,0x05};

// cc9x
static char write_memory85[2]={0x00, 0x9A};
static char write_memory86[2]={0xCC, 0x02};

static char write_memory87[2]={0x00, 0x9C};
static char write_memory88[4]={0xCC, 0x10,0x0E,0x0C};

// ccax
static char write_memory89[2]={0x00, 0xA0};
static char write_memory90[2]={0xCC, 0x0A};

static char write_memory91[2]={0x00, 0xA1};
static char write_memory92[2]={0xCC, 0x06};

// ccbx
static char write_memory93[2]={0x00, 0xB0};
static char write_memory94[2]={0xCC, 0x01};

static char write_memory95[2]={0x00, 0xB2};
static char write_memory96[6]={0xCC, 0x0F,0x0D,0x0B,0x09,0x05};

// cccx
static char write_memory97[2]={0x00, 0xCA};
static char write_memory98[2]={0xCC, 0x02};

static char write_memory99[2]={0x00, 0xCC};
static char write_memory100[4]={0xCC, 0x10,0x0E,0x0C};

// ccdx
static char write_memory101[2]={0x00, 0xD0};
static char write_memory102[2]={0xCC, 0x0A};

static char write_memory103[2]={0x00, 0xD1};
static char write_memory104[2]={0xCC, 0x06};

static int otm8018b_panel_init(struct otm8018b_panel *otm8018b)
{
	struct mipi_dsi_device *dsi = otm8018b->dsi;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS;
	
	/*truly*/
if(0){

	ret = mipi_dsi_generic_write(dsi, &write_memory1, sizeof(write_memory1));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory2, sizeof(write_memory2));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory3, sizeof(write_memory3));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory4, sizeof(write_memory4));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory5, sizeof(write_memory5));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory6, sizeof(write_memory6));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory7, sizeof(write_memory7));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory8, sizeof(write_memory8));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory9, sizeof(write_memory9));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory10, sizeof(write_memory10));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory11, sizeof(write_memory11));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory12, sizeof(write_memory12));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory13, sizeof(write_memory13));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory14, sizeof(write_memory14));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory15, sizeof(write_memory15));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory16, sizeof(write_memory16));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory17, sizeof(write_memory17));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory18, sizeof(write_memory18));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory19, sizeof(write_memory19));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory20, sizeof(write_memory20));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory21, sizeof(write_memory21));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory22, sizeof(write_memory22));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory23, sizeof(write_memory23));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory24, sizeof(write_memory24));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory25, sizeof(write_memory25));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory26, sizeof(write_memory26));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory27, sizeof(write_memory27));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory29, sizeof(write_memory29));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory30, sizeof(write_memory30));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory31, sizeof(write_memory31));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory32, sizeof(write_memory32));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory33, sizeof(write_memory33));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory34, sizeof(write_memory34));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory35, sizeof(write_memory35));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory36, sizeof(write_memory36));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory37, sizeof(write_memory37));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory38, sizeof(write_memory38));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory39, sizeof(write_memory39));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory40, sizeof(write_memory40));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory41, sizeof(write_memory41));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory42, sizeof(write_memory42));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory43, sizeof(write_memory43));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory44, sizeof(write_memory44));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory45, sizeof(write_memory45));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory46, sizeof(write_memory46));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory47, sizeof(write_memory47));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory48, sizeof(write_memory48));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory49, sizeof(write_memory49));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory50, sizeof(write_memory50));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory51, sizeof(write_memory51));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory52, sizeof(write_memory52));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory53, sizeof(write_memory53));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory54, sizeof(write_memory54));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory55, sizeof(write_memory55));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory56, sizeof(write_memory56));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory57, sizeof(write_memory57));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory58, sizeof(write_memory58));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory59, sizeof(write_memory59));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory60, sizeof(write_memory60));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory61, sizeof(write_memory61));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory62, sizeof(write_memory62));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory63, sizeof(write_memory63));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory64, sizeof(write_memory64));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory65, sizeof(write_memory65));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory66, sizeof(write_memory66));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory67, sizeof(write_memory67));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory68, sizeof(write_memory68));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory69, sizeof(write_memory69));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory70, sizeof(write_memory70));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory71, sizeof(write_memory71));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory72, sizeof(write_memory72));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory73, sizeof(write_memory73));
	if (ret < 0)
		return ret;
	ret = mipi_dsi_generic_write(dsi, &write_memory74, sizeof(write_memory74));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory75, sizeof(write_memory75));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory76, sizeof(write_memory76));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory77, sizeof(write_memory77));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory78, sizeof(write_memory78));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory79, sizeof(write_memory79));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory80, sizeof(write_memory80));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory81, sizeof(write_memory81));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory82, sizeof(write_memory82));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory83, sizeof(write_memory83));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory84, sizeof(write_memory84));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory85, sizeof(write_memory85));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory86, sizeof(write_memory86));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory87, sizeof(write_memory87));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory88, sizeof(write_memory88));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory89, sizeof(write_memory89));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory90, sizeof(write_memory90));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory91, sizeof(write_memory91));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory92, sizeof(write_memory92));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory93, sizeof(write_memory93));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory94, sizeof(write_memory94));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory95, sizeof(write_memory95));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory96, sizeof(write_memory96));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory97, sizeof(write_memory97));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory98, sizeof(write_memory98));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory99, sizeof(write_memory99));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory100, sizeof(write_memory100));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory101, sizeof(write_memory101));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory102, sizeof(write_memory102));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory103, sizeof(write_memory103));
	if (ret < 0)
		return ret;

	ret = mipi_dsi_generic_write(dsi, &write_memory104, sizeof(write_memory104));
	mdelay(250);
}
/*truly*/
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0)
		return ret;
	mdelay(5);

	return 0;
}

static int otm8018b_panel_on(struct otm8018b_panel *otm8018b)
{
	struct mipi_dsi_device *dsi = otm8018b->dsi;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		return ret;

	mdelay(10);
	
	return 0;
}

static int otm8018b_panel_off(struct otm8018b_panel *otm8018b)
{
	struct mipi_dsi_device *dsi = otm8018b->dsi;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_write(dsi, 0xff, (u8[]){ 0x10 }, 1);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		return ret;

	msleep(100);

	return 0;
}

static int otm8018b_panel_disable(struct drm_panel *panel)
{
	struct otm8018b_panel *otm8018b = to_otm8018b_panel(panel);

	if (!otm8018b->enabled)
		return 0;

	DRM_DEBUG("disable\n");

	if (otm8018b->backlight) {
		otm8018b->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(otm8018b->backlight);
	}

	otm8018b->enabled = false;

	return 0;
}

static int otm8018b_panel_unprepare(struct drm_panel *panel)
{
	struct otm8018b_panel *otm8018b = to_otm8018b_panel(panel);
	int ret;

	if (!otm8018b->prepared)
		return 0;

	DRM_DEBUG("unprepare\n");

	ret = otm8018b_panel_off(otm8018b);
	if (ret) {
		dev_err(panel->dev, "failed to set panel off: %d\n", ret);
		return ret;
	}

	regulator_disable(otm8018b->supply);
	if (otm8018b->reset_gpio){
		gpiod_set_value(otm8018b->reset_gpio, 0);
		udelay(100);
	}

	otm8018b->prepared = false;

	return 0;
}

static int otm8018b_panel_prepare(struct drm_panel *panel)
{
	struct otm8018b_panel *otm8018b = to_otm8018b_panel(panel);
	int ret;

	if (otm8018b->prepared)
		return 0;

	DRM_DEBUG("prepare\n");

	ret = regulator_enable(otm8018b->supply);
	if (ret < 0)
		return ret;

	if (otm8018b->reset_gpio) {
		gpiod_set_value(otm8018b->reset_gpio, 1);
		mdelay(1);
	}

	if (otm8018b->reset_gpio) {
		gpiod_set_value(otm8018b->reset_gpio, 0);
		usleep_range(50, 50);
	}
	
	if (otm8018b->reset_gpio) {
		gpiod_set_value(otm8018b->reset_gpio, 1);
		mdelay(5);
	}

	ret = otm8018b_panel_init(otm8018b);
	if (ret) {
		dev_err(panel->dev, "failed to init panel: %d\n", ret);
		goto poweroff;
	}

	ret = otm8018b_panel_on(otm8018b);
	if (ret) {
		dev_err(panel->dev, "failed to set panel on: %d\n", ret);
		goto poweroff;
	}

	otm8018b->prepared = true;

	return 0;

poweroff:
	regulator_disable(otm8018b->supply);
	if (otm8018b->reset_gpio)
		gpiod_set_value(otm8018b->reset_gpio, 0);
	return ret;
}

static int otm8018b_panel_enable(struct drm_panel *panel)
{
	struct otm8018b_panel *otm8018b = to_otm8018b_panel(panel);

	if (otm8018b->enabled)
		return 0;

	DRM_DEBUG("enable\n");

	if (otm8018b->backlight) {
		otm8018b->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(otm8018b->backlight);
	}

	otm8018b->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
		.clock = 30857,
		.hdisplay = 480,
		.hsync_start = 480 + 46,
		.hsync_end = 480 + 46 + 4,
		.htotal = 480 + 46 + 4 + 44,
		.vdisplay = 864,
		.vsync_start = 864 + 15,
		.vsync_end = 864 + 15 + 1,
		.vtotal = 864 + 15 + 1 + 16,
		.vrefresh = 60,
};

static int otm8018b_panel_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
				default_mode.hdisplay, default_mode.vdisplay,
				default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm = 64;
	panel->connector->display_info.height_mm = 114;

	return 1;
}

static const struct drm_panel_funcs otm8018b_panel_funcs = {
		.disable = otm8018b_panel_disable,
		.unprepare = otm8018b_panel_unprepare,
		.prepare = otm8018b_panel_prepare,
		.enable = otm8018b_panel_enable,
		.get_modes = otm8018b_panel_get_modes,
};

static const struct of_device_id otm8018b_of_match[] = {
		{ .compatible = "truly,otm8018b", },
		{ }
};
MODULE_DEVICE_TABLE(of, otm8018b_of_match);

static int otm8018b_panel_add(struct otm8018b_panel *otm8018b)
{
	struct device *dev= &otm8018b->dsi->dev;
	struct device_node *np;
	int ret;

	otm8018b->mode = &default_mode;

	otm8018b->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(otm8018b->supply))
		return PTR_ERR(otm8018b->supply);

	otm8018b->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(otm8018b->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(otm8018b->reset_gpio));
		otm8018b->reset_gpio = NULL;
	} else {
		gpiod_direction_output(otm8018b->reset_gpio, 0);
	}

	np = of_parse_phandle(dev->of_node, "backlight", 0);
	if (np) {
		otm8018b->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!otm8018b->backlight)
			return -EPROBE_DEFER;
	}

	drm_panel_init(&otm8018b->base);
	otm8018b->base.funcs = &otm8018b_panel_funcs;
	otm8018b->base.dev = &otm8018b->dsi->dev;

	ret = drm_panel_add(&otm8018b->base);
	if (ret < 0)
		goto put_backlight;

	return 0;

	put_backlight:
	if (otm8018b->backlight)
		put_device(&otm8018b->backlight->dev);

	return ret;
}

static void otm8018b_panel_del(struct otm8018b_panel *otm8018b)
{
	if (otm8018b->base.dev)
		drm_panel_remove(&otm8018b->base);

	if (otm8018b->backlight)
		put_device(&otm8018b->backlight->dev);
}

static int otm8018b_panel_probe(struct mipi_dsi_device *dsi)
{
	struct otm8018b_panel *otm8018b;
	int ret;

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO; 

	otm8018b = devm_kzalloc(&dsi->dev, sizeof(*otm8018b), GFP_KERNEL);
	if (!otm8018b) {
		return -ENOMEM;
	}

	mipi_dsi_set_drvdata(dsi, otm8018b);

	otm8018b->dsi = dsi;

	ret = otm8018b_panel_add(otm8018b);
	if (ret < 0) {
		return ret;
	}

	return mipi_dsi_attach(dsi);
}

static int otm8018b_panel_remove(struct mipi_dsi_device *dsi)
{
	struct otm8018b_panel *otm8018b = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = otm8018b_panel_disable(&otm8018b->base);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", ret);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_detach(&otm8018b->base);
	otm8018b_panel_del(otm8018b);

	return 0;
}

static void otm8018b_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct otm8018b_panel *otm8018b = mipi_dsi_get_drvdata(dsi);

	otm8018b_panel_disable(&otm8018b->base);
}

static struct mipi_dsi_driver otm8018b_panel_driver = {
	.driver = {
		.name = "panel-truly-otm8018b",
		.of_match_table = otm8018b_of_match,
	},
	.probe = otm8018b_panel_probe,
	.remove = otm8018b_panel_remove,
	.shutdown = otm8018b_panel_shutdown,
};
module_mipi_dsi_driver(otm8018b_panel_driver);

MODULE_AUTHOR("Vinay Simha BN <vinaysimha@inforcecomputing.com>");
MODULE_DESCRIPTION("TRULY 480x864 panel driver");
MODULE_LICENSE("GPL v2");
