// SPDX-License-Identifier: GPL-2.0
/*
 * DSI Panel Driver for PNC357DB1-4
 *
 * Author: Panda <panda@bredos.org>
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#define pnc357db1_INIT_CMD_LEN	2

struct pnc357db1_init_cmd {
	u8 data[pnc357db1_INIT_CMD_LEN];
};

struct pnc357db1_panel_desc {
	const struct drm_display_mode mode;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	const struct pnc357db1_init_cmd *init_cmds;
	u32 num_init_cmds;
};

struct pnc357db1 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct pnc357db1_panel_desc *desc;

	struct regulator *vcc_avee;
	struct gpio_desc *reset;
};

static inline struct pnc357db1 *panel_to_pnc357db1(struct drm_panel *panel)
{
	return container_of(panel, struct pnc357db1, panel);
}

static int pnc357db1_prepare(struct drm_panel *panel)
{
	struct pnc357db1 *pnc357db1 = panel_to_pnc357db1(panel);
	int ret;

	ret = regulator_enable(pnc357db1->vcc_avee);
	if (ret)
		return ret;

	gpiod_set_value(pnc357db1->reset, 0);
	msleep(120);
	gpiod_set_value(pnc357db1->reset, 1);
	msleep(120);

	return 0;
}

static int pnc357db1_enable(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct pnc357db1 *pnc357db1 = panel_to_pnc357db1(panel);
	const struct pnc357db1_panel_desc *desc = pnc357db1->desc;
	struct mipi_dsi_device *dsi = pnc357db1->dsi;
	unsigned int i;
	int err;

	msleep(10);

	for (i = 0; i < desc->num_init_cmds; i++) {
		const struct pnc357db1_init_cmd *cmd = &desc->init_cmds[i];

		err = mipi_dsi_dcs_write_buffer(dsi, cmd->data, pnc357db1_INIT_CMD_LEN);
		if (err < 0)
			return err;
	}

	msleep(120);

	err = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (err < 0)
		DRM_DEV_ERROR(dev, "failed to exit sleep mode ret = %d\n", err);

	err =  mipi_dsi_dcs_set_display_on(dsi);
	if (err < 0)
		DRM_DEV_ERROR(dev, "failed to set display on ret = %d\n", err);

	return 0;
}

static int pnc357db1_disable(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct pnc357db1 *pnc357db1 = panel_to_pnc357db1(panel);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(pnc357db1->dsi);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(pnc357db1->dsi);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "failed to enter sleep mode: %d\n", ret);

	return 0;
}

static int pnc357db1_unprepare(struct drm_panel *panel)
{
	struct pnc357db1 *pnc357db1 = panel_to_pnc357db1(panel);

	gpiod_set_value(pnc357db1->reset, 1);
	msleep(120);

	regulator_disable(pnc357db1->vcc_avee);

	return 0;
}

static int pnc357db1_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct pnc357db1 *pnc357db1 = panel_to_pnc357db1(panel);
	const struct drm_display_mode *desc_mode = &pnc357db1->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, desc_mode);
	if (!mode) {
		DRM_DEV_ERROR(&pnc357db1->dsi->dev, "failed to add mode %ux%ux@%u\n",
			      desc_mode->hdisplay, desc_mode->vdisplay,
			      drm_mode_vrefresh(desc_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs pnc357db1_funcs = {
	.prepare = pnc357db1_prepare,
	.enable = pnc357db1_enable,
	.disable = pnc357db1_disable,
	.unprepare = pnc357db1_unprepare,
	.get_modes = pnc357db1_get_modes,
};

static const struct pnc357db1_init_cmd pnc357db1_init_cmds[] = {
	{ .data = { 0x0a, 0x31 } },
	{ .data = { 0x58, 0x11 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x89, 0x30 } },
	{ .data = { 0x80, 0x0A } },
	{ .data = { 0x00, 0x06 } },
	{ .data = { 0x40, 0x00 } },
	{ .data = { 0x28, 0x06 } },
	{ .data = { 0x40, 0x06 } },
	{ .data = { 0x40, 0x02 } },
	{ .data = { 0x00, 0x04 } },
	{ .data = { 0x21, 0x00 } },
	{ .data = { 0x20, 0x05 } },
	{ .data = { 0xD0, 0x00 } },
	{ .data = { 0x16, 0x00 } },
	{ .data = { 0x0C, 0x02 } },
	{ .data = { 0x77, 0x00 } },
	{ .data = { 0xDA, 0x18 } },
	{ .data = { 0x00, 0x10 } },
	{ .data = { 0xE0, 0x03 } },
	{ .data = { 0x0C, 0x20 } },
	{ .data = { 0x00, 0x06 } },
	{ .data = { 0x0B, 0x0B } },
	{ .data = { 0x33, 0x0E } },
	{ .data = { 0x1C, 0x2A } },
	{ .data = { 0x38, 0x46 } },
	{ .data = { 0x54, 0x62 } },
	{ .data = { 0x69, 0x70 } },
	{ .data = { 0x77, 0x79 } },
	{ .data = { 0x7B, 0x7D } },
	{ .data = { 0x7E, 0x01 } },
	{ .data = { 0x02, 0x01 } },
	{ .data = { 0x00, 0x09 } },
	{ .data = { 0x40, 0x09 } },
	{ .data = { 0xBE, 0x19 } },
	{ .data = { 0xFC, 0x19 } },
	{ .data = { 0xFA, 0x19 } },
	{ .data = { 0xF8, 0x1A } },
	{ .data = { 0x38, 0x1A } },
	{ .data = { 0x78, 0x1A } },
	{ .data = { 0xB6, 0x2A } },
	{ .data = { 0xF6, 0x2B } },
	{ .data = { 0x34, 0x2B } },
	{ .data = { 0x74, 0x3B } },
	{ .data = { 0x74, 0x6B } },
	{ .data = { 0x74, 0x39 } },
	{ .data = { 0x00, 0x06 } },
	{ .data = { 0xB9, 0x83 } },
	{ .data = { 0x12, 0x1A } },
	{ .data = { 0x55, 0x00 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x03, 0x51 } },
	{ .data = { 0x08, 0x00 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0x53 } },
	{ .data = { 0x24, 0x39 } },
	{ .data = { 0x00, 0x1D } },
	{ .data = { 0xB1, 0x1C } },
	{ .data = { 0x6B, 0x6B } },
	{ .data = { 0x27, 0xE7 } },
	{ .data = { 0x00, 0x1B } },
	{ .data = { 0x12, 0x20 } },
	{ .data = { 0x20, 0x2D } },
	{ .data = { 0x2D, 0x1F } },
	{ .data = { 0x33, 0x31 } },
	{ .data = { 0x40, 0xCD } },
	{ .data = { 0xFF, 0x1A } },
	{ .data = { 0x05, 0x15 } },
	{ .data = { 0x98, 0x00 } },
	{ .data = { 0x88, 0xF9 } },
	{ .data = { 0xFF, 0xFF } },
	{ .data = { 0xCF, 0x39 } },
	{ .data = { 0x00, 0x12 } },
	{ .data = { 0xB2, 0x00 } },
	{ .data = { 0x6A, 0x40 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x14, 0x6E } },
	{ .data = { 0x40, 0x73 } },
	{ .data = { 0x02, 0x80 } },
	{ .data = { 0x21, 0x21 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x10, 0x27 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x2D, 0xB4 } },
	{ .data = { 0x64, 0x00 } },
	{ .data = { 0x08, 0x7F } },
	{ .data = { 0x08, 0x7F } },
	{ .data = { 0x00, 0x62 } },
	{ .data = { 0x01, 0x72 } },
	{ .data = { 0x01, 0x72 } },
	{ .data = { 0x00, 0x60 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x0A, 0x08 } },
	{ .data = { 0x00, 0x29 } },
	{ .data = { 0x05, 0x05 } },
	{ .data = { 0x05, 0x00 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0xFF, 0x00 } },
	{ .data = { 0xFF, 0x14 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x0F, 0x0F } },
	{ .data = { 0x2D, 0x2D } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x04, 0xB6 } },
	{ .data = { 0x8F, 0x8F } },
	{ .data = { 0x03, 0x39 } },
	{ .data = { 0x00, 0x03 } },
	{ .data = { 0xBC, 0x06 } },
	{ .data = { 0x02, 0x39 } },
	{ .data = { 0x00, 0x07 } },
	{ .data = { 0xC0, 0x34 } },
	{ .data = { 0x34, 0x44 } },
	{ .data = { 0x00, 0x08 } },
	{ .data = { 0xD8, 0x39 } },
	{ .data = { 0x00, 0x06 } },
	{ .data = { 0xC9, 0x00 } },
	{ .data = { 0x1E, 0x80 } },
	{ .data = { 0xA5, 0x01 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x07, 0xCB } },
	{ .data = { 0x00, 0x13 } },
	{ .data = { 0x38, 0x00 } },
	{ .data = { 0x0B, 0x27 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xCC } },
	{ .data = { 0x02, 0x39 } },
	{ .data = { 0x00, 0x02 } },
	{ .data = { 0xD1, 0x07 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x29, 0xD3 } },
	{ .data = { 0x00, 0xC0 } },
	{ .data = { 0x08, 0x08 } },
	{ .data = { 0x08, 0x04 } },
	{ .data = { 0x04, 0x04 } },
	{ .data = { 0x14, 0x02 } },
	{ .data = { 0x07, 0x07 } },
	{ .data = { 0x07, 0x31 } },
	{ .data = { 0x13, 0x12 } },
	{ .data = { 0x12, 0x12 } },
	{ .data = { 0x03, 0x03 } },
	{ .data = { 0x03, 0x32 } },
	{ .data = { 0x10, 0x11 } },
	{ .data = { 0x00, 0x11 } },
	{ .data = { 0x32, 0x10 } },
	{ .data = { 0x03, 0x00 } },
	{ .data = { 0x03, 0x32 } },
	{ .data = { 0x10, 0x03 } },
	{ .data = { 0x00, 0x03 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0xFF, 0x00 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x31, 0xD5 } },
	{ .data = { 0x19, 0x19 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x02, 0x02 } },
	{ .data = { 0x03, 0x03 } },
	{ .data = { 0x04, 0x04 } },
	{ .data = { 0x05, 0x05 } },
	{ .data = { 0x06, 0x06 } },
	{ .data = { 0x07, 0x07 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x01, 0x01 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x40, 0x40 } },
	{ .data = { 0x20, 0x20 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x40, 0x40 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x2F, 0x2F } },
	{ .data = { 0x31, 0x31 } },
	{ .data = { 0x2F, 0x2F } },
	{ .data = { 0x31, 0x31 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x41, 0x41 } },
	{ .data = { 0x41, 0x41 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x31, 0xD6 } },
	{ .data = { 0x40, 0x40 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x05, 0x05 } },
	{ .data = { 0x04, 0x04 } },
	{ .data = { 0x03, 0x03 } },
	{ .data = { 0x02, 0x02 } },
	{ .data = { 0x01, 0x01 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x07, 0x07 } },
	{ .data = { 0x06, 0x06 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x19, 0x19 } },
	{ .data = { 0x20, 0x20 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x40, 0x40 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x2F, 0x2F } },
	{ .data = { 0x31, 0x31 } },
	{ .data = { 0x2F, 0x2F } },
	{ .data = { 0x31, 0x31 } },
	{ .data = { 0x18, 0x18 } },
	{ .data = { 0x41, 0x41 } },
	{ .data = { 0x41, 0x41 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x40, 0xE1 } },
	{ .data = { 0x11, 0x00 } },
	{ .data = { 0x00, 0x89 } },
	{ .data = { 0x30, 0x80 } },
	{ .data = { 0x0A, 0x00 } },
	{ .data = { 0x06, 0x40 } },
	{ .data = { 0x00, 0x28 } },
	{ .data = { 0x06, 0x40 } },
	{ .data = { 0x06, 0x40 } },
	{ .data = { 0x02, 0x00 } },
	{ .data = { 0x04, 0x21 } },
	{ .data = { 0x00, 0x20 } },
	{ .data = { 0x05, 0xD0 } },
	{ .data = { 0x00, 0x16 } },
	{ .data = { 0x00, 0x0C } },
	{ .data = { 0x02, 0x77 } },
	{ .data = { 0x00, 0xDA } },
	{ .data = { 0x18, 0x00 } },
	{ .data = { 0x10, 0xE0 } },
	{ .data = { 0x03, 0x0C } },
	{ .data = { 0x20, 0x00 } },
	{ .data = { 0x06, 0x0B } },
	{ .data = { 0x0B, 0x33 } },
	{ .data = { 0x0E, 0x1C } },
	{ .data = { 0x2A, 0x38 } },
	{ .data = { 0x46, 0x54 } },
	{ .data = { 0x62, 0x69 } },
	{ .data = { 0x70, 0x77 } },
	{ .data = { 0x79, 0x7B } },
	{ .data = { 0x7D, 0x7E } },
	{ .data = { 0x01, 0x02 } },
	{ .data = { 0x01, 0x00 } },
	{ .data = { 0x09, 0x39 } },
	{ .data = { 0x00, 0x0C } },
	{ .data = { 0xE7, 0x06 } },
	{ .data = { 0x14, 0x14 } },
	{ .data = { 0x1A, 0x23 } },
	{ .data = { 0x38, 0x00 } },
	{ .data = { 0x23, 0x5D } },
	{ .data = { 0x02, 0x02 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xBD } },
	{ .data = { 0x01, 0x39 } },
	{ .data = { 0x00, 0x04 } },
	{ .data = { 0xB1, 0x01 } },
	{ .data = { 0x23, 0x00 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x25, 0xD8 } },
	{ .data = { 0x20, 0x00 } },
	{ .data = { 0x02, 0x22 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x20, 0x00 } },
	{ .data = { 0x02, 0x22 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x20, 0x00 } },
	{ .data = { 0x02, 0x22 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x20, 0x00 } },
	{ .data = { 0x02, 0x22 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x20, 0x00 } },
	{ .data = { 0x02, 0x22 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x20, 0x00 } },
	{ .data = { 0x02, 0x22 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x1A, 0xE1 } },
	{ .data = { 0x40, 0x09 } },
	{ .data = { 0xBE, 0x19 } },
	{ .data = { 0xFC, 0x19 } },
	{ .data = { 0xFA, 0x19 } },
	{ .data = { 0xF8, 0x1A } },
	{ .data = { 0x38, 0x1A } },
	{ .data = { 0x78, 0x1A } },
	{ .data = { 0xB6, 0x2A } },
	{ .data = { 0xF6, 0x2B } },
	{ .data = { 0x34, 0x2B } },
	{ .data = { 0x74, 0x3B } },
	{ .data = { 0x74, 0x6B } },
	{ .data = { 0xF4, 0x39 } },
	{ .data = { 0x00, 0x0D } },
	{ .data = { 0xE7, 0x02 } },
	{ .data = { 0x00, 0x40 } },
	{ .data = { 0x01, 0x84 } },
	{ .data = { 0x13, 0xBE } },
	{ .data = { 0x14, 0x48 } },
	{ .data = { 0x00, 0x04 } },
	{ .data = { 0x26, 0x39 } },
	{ .data = { 0x00, 0x08 } },
	{ .data = { 0xCB, 0x1F } },
	{ .data = { 0x55, 0x03 } },
	{ .data = { 0x28, 0x0D } },
	{ .data = { 0x08, 0x0A } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xBD } },
	{ .data = { 0x02, 0x39 } },
	{ .data = { 0x00, 0x0D } },
	{ .data = { 0xD8, 0xAF } },
	{ .data = { 0xFF, 0xFA } },
	{ .data = { 0xFA, 0xBF } },
	{ .data = { 0xEA, 0xAF } },
	{ .data = { 0xFF, 0xFA } },
	{ .data = { 0xFA, 0xBF } },
	{ .data = { 0xEA, 0x39 } },
	{ .data = { 0x00, 0x23 } },
	{ .data = { 0xE7, 0x01 } },
	{ .data = { 0x05, 0x01 } },
	{ .data = { 0x03, 0x01 } },
	{ .data = { 0x03, 0x04 } },
	{ .data = { 0x02, 0x02 } },
	{ .data = { 0x24, 0x00 } },
	{ .data = { 0x24, 0x81 } },
	{ .data = { 0x02, 0x40 } },
	{ .data = { 0x00, 0x29 } },
	{ .data = { 0x60, 0x03 } },
	{ .data = { 0x02, 0x01 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x00, 0x39 } },
	{ .data = { 0x00, 0x02 } },
	{ .data = { 0xBD, 0x03 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x19, 0xD8 } },
	{ .data = { 0xAA, 0xAA } },
	{ .data = { 0xAA, 0xAB } },
	{ .data = { 0xBF, 0xEA } },
	{ .data = { 0xAA, 0xAA } },
	{ .data = { 0xAA, 0xAB } },
	{ .data = { 0xBF, 0xEA } },
	{ .data = { 0xAF, 0xFF } },
	{ .data = { 0xFA, 0xFA } },
	{ .data = { 0xBF, 0xEA } },
	{ .data = { 0xAF, 0xFF } },
	{ .data = { 0xFA, 0xFA } },
	{ .data = { 0xBF, 0xEA } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x03, 0xE1 } },
	{ .data = { 0x01, 0x3F } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xBD } },
	{ .data = { 0x00, 0x39 } },
	{ .data = { 0x00, 0x2F } },
	{ .data = { 0xE0, 0x00 } },
	{ .data = { 0x13, 0x30 } },
	{ .data = { 0x36, 0x40 } },
	{ .data = { 0x78, 0x8B } },
	{ .data = { 0x94, 0x95 } },
	{ .data = { 0x97, 0x94 } },
	{ .data = { 0x94, 0x91 } },
	{ .data = { 0x8F, 0x8F } },
	{ .data = { 0x8B, 0x8A } },
	{ .data = { 0x8C, 0x8E } },
	{ .data = { 0xA6, 0xB7 } },
	{ .data = { 0x4D, 0x7F } },
	{ .data = { 0x00, 0x13 } },
	{ .data = { 0x30, 0x36 } },
	{ .data = { 0x40, 0x78 } },
	{ .data = { 0x8B, 0x94 } },
	{ .data = { 0x95, 0x97 } },
	{ .data = { 0x94, 0x94 } },
	{ .data = { 0x91, 0x8F } },
	{ .data = { 0x8F, 0x8B } },
	{ .data = { 0x8A, 0x8C } },
	{ .data = { 0x8E, 0xA6 } },
	{ .data = { 0xB7, 0x4D } },
	{ .data = { 0x7F, 0x39 } },
	{ .data = { 0x00, 0x05 } },
	{ .data = { 0xBA, 0x70 } },
	{ .data = { 0x03, 0xA8 } },
	{ .data = { 0x92, 0x39 } },
	{ .data = { 0x00, 0x25 } },
	{ .data = { 0xD8, 0xEA } },
	{ .data = { 0xAA, 0xAA } },
	{ .data = { 0xAE, 0xAA } },
	{ .data = { 0xAF, 0xEA } },
	{ .data = { 0xAA, 0xAA } },
	{ .data = { 0xAE, 0xAA } },
	{ .data = { 0xAF, 0xE0 } },
	{ .data = { 0x00, 0x0A } },
	{ .data = { 0x2E, 0x80 } },
	{ .data = { 0x2F, 0xE0 } },
	{ .data = { 0x00, 0x0A } },
	{ .data = { 0x2E, 0x80 } },
	{ .data = { 0x2F, 0xE0 } },
	{ .data = { 0x00, 0x0A } },
	{ .data = { 0x2E, 0x80 } },
	{ .data = { 0x2F, 0xE0 } },
	{ .data = { 0x00, 0x0A } },
	{ .data = { 0x2E, 0x80 } },
	{ .data = { 0x2F, 0x39 } },
	{ .data = { 0x00, 0x02 } },
	{ .data = { 0xBD, 0x00 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xC1 } },
	{ .data = { 0x01, 0x39 } },
	{ .data = { 0x00, 0x02 } },
	{ .data = { 0xBD, 0x01 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x3B, 0xC1 } },
	{ .data = { 0x00, 0x04 } },
	{ .data = { 0x08, 0x0C } },
	{ .data = { 0x10, 0x14 } },
	{ .data = { 0x18, 0x1C } },
	{ .data = { 0x1F, 0x23 } },
	{ .data = { 0x27, 0x2B } },
	{ .data = { 0x2F, 0x33 } },
	{ .data = { 0x37, 0x3B } },
	{ .data = { 0x3F, 0x43 } },
	{ .data = { 0x47, 0x4B } },
	{ .data = { 0x52, 0x5A } },
	{ .data = { 0x62, 0x69 } },
	{ .data = { 0x71, 0x79 } },
	{ .data = { 0x81, 0x89 } },
	{ .data = { 0x91, 0x98 } },
	{ .data = { 0xA1, 0xA9 } },
	{ .data = { 0xB1, 0xB9 } },
	{ .data = { 0xC1, 0xCA } },
	{ .data = { 0xD2, 0xDA } },
	{ .data = { 0xE3, 0xEA } },
	{ .data = { 0xF4, 0xF8 } },
	{ .data = { 0xF9, 0xFB } },
	{ .data = { 0xFD, 0xFF } },
	{ .data = { 0x16, 0xA4 } },
	{ .data = { 0x44, 0x16 } },
	{ .data = { 0x90, 0xE7 } },
	{ .data = { 0xF9, 0x71 } },
	{ .data = { 0xA0, 0xF3 } },
	{ .data = { 0x1F, 0x40 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xBD } },
	{ .data = { 0x02, 0x39 } },
	{ .data = { 0x00, 0x3B } },
	{ .data = { 0xC1, 0x00 } },
	{ .data = { 0x04, 0x08 } },
	{ .data = { 0x0C, 0x10 } },
	{ .data = { 0x14, 0x18 } },
	{ .data = { 0x1C, 0x20 } },
	{ .data = { 0x24, 0x28 } },
	{ .data = { 0x2D, 0x31 } },
	{ .data = { 0x35, 0x39 } },
	{ .data = { 0x3D, 0x41 } },
	{ .data = { 0x45, 0x49 } },
	{ .data = { 0x4D, 0x55 } },
	{ .data = { 0x5D, 0x65 } },
	{ .data = { 0x6D, 0x75 } },
	{ .data = { 0x7D, 0x85 } },
	{ .data = { 0x8D, 0x94 } },
	{ .data = { 0x9C, 0xA4 } },
	{ .data = { 0xAC, 0xB4 } },
	{ .data = { 0xBC, 0xC4 } },
	{ .data = { 0xCC, 0xD4 } },
	{ .data = { 0xDC, 0xE4 } },
	{ .data = { 0xEC, 0xF4 } },
	{ .data = { 0xF8, 0xFA } },
	{ .data = { 0xFC, 0xFE } },
	{ .data = { 0xFF, 0x06 } },
	{ .data = { 0xAA, 0xFC } },
	{ .data = { 0x5B, 0xFF } },
	{ .data = { 0xFF, 0xA4 } },
	{ .data = { 0xF9, 0x86 } },
	{ .data = { 0xF9, 0x55 } },
	{ .data = { 0x40, 0x39 } },
	{ .data = { 0x00, 0x02 } },
	{ .data = { 0xBD, 0x03 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x3B, 0xC1 } },
	{ .data = { 0x00, 0x04 } },
	{ .data = { 0x07, 0x0B } },
	{ .data = { 0x0F, 0x13 } },
	{ .data = { 0x17, 0x1B } },
	{ .data = { 0x1F, 0x23 } },
	{ .data = { 0x27, 0x2C } },
	{ .data = { 0x30, 0x33 } },
	{ .data = { 0x38, 0x3C } },
	{ .data = { 0x40, 0x44 } },
	{ .data = { 0x48, 0x4C } },
	{ .data = { 0x53, 0x5B } },
	{ .data = { 0x63, 0x6B } },
	{ .data = { 0x72, 0x7A } },
	{ .data = { 0x82, 0x89 } },
	{ .data = { 0x91, 0x99 } },
	{ .data = { 0xA1, 0xA9 } },
	{ .data = { 0xB1, 0xB9 } },
	{ .data = { 0xC1, 0xC9 } },
	{ .data = { 0xD1, 0xDA } },
	{ .data = { 0xE2, 0xEA } },
	{ .data = { 0xF3, 0xF6 } },
	{ .data = { 0xF9, 0xFA } },
	{ .data = { 0xFE, 0xFF } },
	{ .data = { 0x0F, 0x9A } },
	{ .data = { 0xFC, 0x31 } },
	{ .data = { 0x40, 0xE4 } },
	{ .data = { 0xFB, 0xE9 } },
	{ .data = { 0xA3, 0xD9 } },
	{ .data = { 0x77, 0x00 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xBD } },
	{ .data = { 0x02, 0x39 } },
	{ .data = { 0x00, 0x02 } },
	{ .data = { 0xBF, 0x72 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xBD } },
	{ .data = { 0x00, 0x39 } },
	{ .data = { 0x00, 0x08 } },
	{ .data = { 0xBF, 0xFD } },
	{ .data = { 0x00, 0x80 } },
	{ .data = { 0x9C, 0x10 } },
	{ .data = { 0x00, 0x80 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xE9 } },
	{ .data = { 0xDE, 0x39 } },
	{ .data = { 0x00, 0x04 } },
	{ .data = { 0xB1, 0xCC } },
	{ .data = { 0x03, 0x00 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xE9 } },
	{ .data = { 0x3F, 0x39 } },
	{ .data = { 0x00, 0x07 } },
	{ .data = { 0xD0, 0x07 } },
	{ .data = { 0xC0, 0x08 } },
	{ .data = { 0x03, 0x11 } },
	{ .data = { 0x00, 0x39 } },
	{ .data = { 0x00, 0x03 } },
	{ .data = { 0xB0, 0x00 } },
	{ .data = { 0x00, 0x39 } },
	{ .data = { 0x00, 0x02 } },
	{ .data = { 0xE9, 0xCF } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x02, 0xBA } },
	{ .data = { 0x03, 0x39 } },
	{ .data = { 0x00, 0x02 } },
	{ .data = { 0xE9, 0x3F } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x01, 0x11 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x01, 0x29 } },
};

static const struct pnc357db1_panel_desc pnc357db1_desc = {
	.mode = {
		.clock           = 275000000,

		.hdisplay        = 1600,       // Horizontal active pixels
		.hsync_start     = 1600 + 60,  // Horizontal active + Front Porch
		.hsync_end       = 1600 + 60 + 20, // Horizontal sync start + Sync Length
		.htotal          = 1600 + 60 + 20 + 20, // Horizontal sync end + Back Porch

		.vdisplay        = 2560,       // Vertical active pixels
		.vsync_start     = 2560 + 112, // Vertical active + Front Porch
		.vsync_end       = 2560 + 112 + 4, // Vertical sync start + Sync Length
		.vtotal          = 2560 + 112 + 4 + 18, // Vertical sync end + Back Porch

		.width_mm        = 266,
		.height_mm       = 166, 
		.type            = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.init_cmds = pnc357db1_init_cmds,
	.num_init_cmds = ARRAY_SIZE(pnc357db1_init_cmds),
};

static int pnc357db1_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct pnc357db1_panel_desc *desc;
	struct pnc357db1 *pnc357db1;
	int ret;

	pnc357db1 = devm_kzalloc(&dsi->dev, sizeof(pnc357db1), GFP_KERNEL);
	if (!pnc357db1)
		return -ENOMEM;

	desc = of_device_get_match_data(dev);
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_MODE_LPM;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	pnc357db1->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(pnc357db1->reset)) {
		DRM_DEV_ERROR(&dsi->dev, "failed to get reset GPIO\n");
		return PTR_ERR(pnc357db1->reset);
	}

	pnc357db1->vcc_avee = devm_regulator_get(dev, "vcc_avee");
	if (IS_ERR(pnc357db1->vcc_avee)) {
		DRM_DEV_ERROR(&dsi->dev, "failed to get vcc_avee regulator\n");
		return PTR_ERR(pnc357db1->vcc_avee);
	}

	drm_panel_init(&pnc357db1->panel, dev, &pnc357db1_funcs,
				DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&pnc357db1->panel);
	if (ret)
		return ret;

	drm_panel_add(&pnc357db1->panel);

	mipi_dsi_set_drvdata(dsi, pnc357db1);
	pnc357db1->dsi = dsi;
	pnc357db1->desc = desc;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&pnc357db1->panel);

	return ret;
}

static void pnc357db1_remove(struct mipi_dsi_device *dsi)
{
	struct pnc357db1 *pnc357db1 = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&pnc357db1->panel);
}

static const struct of_device_id pnc357db1_of_match[] = {
	{ .compatible = "pnc357db1", .data = &pnc357db1_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pnc357db1_of_match);

static struct mipi_dsi_driver pnc357db1_driver = {
	.probe = pnc357db1_probe,
	.remove = pnc357db1_remove,
	.driver = {
		.name = "pnc357db1",
		.of_match_table = pnc357db1_of_match,
	},
};
module_mipi_dsi_driver(pnc357db1_driver);

MODULE_AUTHOR("Panda <panda@bredos.org>");
MODULE_DESCRIPTION("PNC357DB1 DSI Panel Driver");
MODULE_LICENSE("GPL");