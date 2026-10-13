/*----------------------------------------------------------------------------*/
/*
	ODROID-GOADV LCD control commands
*/
/*----------------------------------------------------------------------------*/
#include <asm/unaligned.h>
#include <config.h>
#include <common.h>
#include <errno.h>
#include <linux/media-bus-format.h>
#include <malloc.h>
#include <video.h>
#include <video_console.h>
#include <video_rockchip.h>
#include <video_font.h>
#include <dm/device.h>
#include <dm/uclass.h>
#include <dm/uclass-internal.h>
#include <fat.h>

/*----------------------------------------------------------------------------*/
#include "rockchip_display.h"
#include "bmp_helper.h"
#include <rockchip_display_cmds.h>
#include <odroidgoa_status.h>
#include <bmp_layout.h>
#include "logo_embedded.h"

/*----------------------------------------------------------------------------*/
struct lcd *lcd = NULL;

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
void lcd_sync(void)
{
	flush_dcache_range((ulong)lcd->drm_fb_mem,
		ALIGN((ulong)lcd->drm_fb_mem + lcd->drm_fb_size,
		CONFIG_SYS_CACHELINE_SIZE));
}

/*----------------------------------------------------------------------------*/
int lcd_getrot(void)
{
	unsigned long rot;
	unsigned long default_rot;
	switch(get_rg351_rev())
	{
		case MODEL_RG351P:
			default_rot = LCD_ROTATE_270;
		break;
			
		case MODEL_RG351MP:
		case MODEL_RG351V:
		default:
			default_rot = LCD_ROTATE_0;
		break;
	
	}
	/* default lcd rotation setup*/
	if (NULL == env_get("lcd_rotate"))
		env_set_ulong("lcd_rotate", default_rot);

	rot = simple_strtoul(env_get("lcd_rotate"), NULL, 10);
	if (rot > LCD_ROTATE_270) {
		env_set_ulong("lcd_rotate", default_rot);
		rot = default_rot;
	}
	return rot;
}

/*----------------------------------------------------------------------------*/
int lcd_init(void)
{
	if (lcd != NULL)
		return 0;

	lcd = (struct lcd *)malloc(sizeof(struct lcd));
	if (!lcd) {
		printf("%s : lcd memory allocation error!\n", __func__);
		return -ENOMEM;
	}
	memset((void *)lcd, 0x00, sizeof(struct lcd));

	if (uclass_get_device(UCLASS_VIDEO, 0, &lcd->udev_video)) {
		printf("%s: UCLASS_VIDEO load fail!\n", __func__);
		goto error;
	}
	if (uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &lcd->udev_vidcon)) {
		printf("%s: UCLASS_VIDEO_CONSOLE load fail!\n", __func__);
		goto error;
	}

	if ((lcd->s = get_display_state()) == NULL) {
		printf("%s : Display get state fail!\n", __func__);
		goto error;
	}

	lcd->rot = lcd_getrot();
	lcd->bpp = 24;

	lcd->w = lcd->s->conn_state.mode.hdisplay;
	lcd->h = lcd->s->conn_state.mode.vdisplay;

	lcd->drm_fb_mem  = get_drm_memory();
	lcd->drm_fb_size = (lcd->w * lcd->h * lcd->bpp) >> 3;

	lcd->s->crtc_state.format  = ROCKCHIP_FMT_RGB888;
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
	lcd->s->crtc_state.rb_swap = true;
#else
	lcd->s->crtc_state.rb_swap = false;
#endif
	lcd->s->crtc_state.ymirror = 0;
	lcd->s->crtc_state.src_w = lcd->s->crtc_state.crtc_w = lcd->w;
	lcd->s->crtc_state.src_h = lcd->s->crtc_state.crtc_h = lcd->h;
	lcd->s->crtc_state.src_x = lcd->s->crtc_state.crtc_x = 0;
	lcd->s->crtc_state.src_y = lcd->s->crtc_state.crtc_y = 0;

	lcd->s->crtc_state.dma_addr = (u32)lcd->drm_fb_mem;
	lcd->s->crtc_state.xvir =
		ALIGN(lcd->s->crtc_state.src_w * lcd->bpp, 32) >> 5;

	memset((void *)lcd->drm_fb_mem, 0x00, lcd->drm_fb_size);

	lcd->fg_color.r = lcd->fg_color.g = lcd->fg_color.b = 0xff;
	lcd->bg_color.r = lcd->bg_color.g = lcd->bg_color.b = 0;

	lcd_sync();

	/* lcd enable */
	set_display_state(lcd->s, true);
	return 0;
error:
	printf("LCD initialize fail!\n");
	free(lcd);	lcd = NULL;
	return -ENODEV;
}

int lcd_onoff(bool onoff)
{
	if (lcd == NULL)
		return -ENODEV;;

	set_display_state(lcd->s, onoff);

	return 0;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
int show_bmp(unsigned long bmp_mem)
{
	struct bmp_header *header;

	header = (struct bmp_header *)bmp_mem;

	if (bmpdecoder((void *)bmp_mem,
		(void *)lcd->drm_fb_mem,
		header->bit_count)) {
		printf("%s : failed to decode bmp at 0x%p\n",
			__func__, header);
		return -1;
	}
	lcd_sync ();
	return 0;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
static int do_show_bmp(cmd_tbl_t *cmdtp, int flag, int argc,
				char *const argv[])
{
	int ret = 0;

	if ((ret = lcd_init()))
		return	ret;

	if (argc != 2)
		return CMD_RET_USAGE;

	ret = show_bmp(simple_strtoul(argv[1], NULL, 16));

	return ret;
}

U_BOOT_CMD(
	show_bmp, 2, 1, do_show_bmp,
	"Display a bmp image loaded in memory",
	" <bmp_load_addr>"
);

/*----------------------------------------------------------------------------*/
static int lcd_get_width(void)
{
	if ((lcd->rot == 1) || (lcd->rot == 3))
		return lcd->h;
	else
		return lcd->w;
}

/*----------------------------------------------------------------------------*/
static int lcd_get_height(void)
{
	if ((lcd->rot == 1) || (lcd->rot == 3))
		return lcd->w;
	else
		return lcd->h;
}

/*----------------------------------------------------------------------------*/
int lcd_setcur(unsigned long x, unsigned long y)
{
	int lcd_w, lcd_h;
	if (lcd == NULL)
		return -ENODEV;

	lcd_w = lcd_get_width();
	lcd_h = lcd_get_height();

	x = lcd_w < x ? lcd_w : x;
	y = lcd_h < y ? lcd_h : y;

	vidconsole_position_cursor(lcd->udev_vidcon, x, y);
	return 0;
}

/*----------------------------------------------------------------------------*/
int lcd_setline(unsigned long line)
{
	int lcd_h;
	if (lcd == NULL)
		return -ENODEV;

	if (line > ((lcd_get_height() / VIDEO_FONT_HEIGHT) -1)) {
		printf("lcd line overflow (line max = %d)\n",
			(lcd_get_height() / VIDEO_FONT_HEIGHT) -1);
		return -1;
	}
	lcd_h = line * VIDEO_FONT_HEIGHT;

	return lcd_setcur(0, lcd_h);
}

/*----------------------------------------------------------------------------*/
int lcd_putstr(const char *str)
{
	if (lcd == NULL)
		return -ENODEV;

	while (*str != 0x00)
		vidconsole_put_char(lcd->udev_vidcon, *str++);

	lcd_sync ();
	return 0;
}

/*----------------------------------------------------------------------------*/
int lcd_clear(void)
{
	int cnt;
	struct lcd_fb_bit *fb, *bg;

	if (lcd == NULL)
		return -ENODEV;

	fb = (struct lcd_fb_bit *)lcd->drm_fb_mem;
	bg = (struct lcd_fb_bit *)&lcd->bg_color;

	for (cnt = 0; cnt < lcd->w * lcd->h; cnt++)
		*fb++ = *bg;

	lcd_setline(0);
	lcd_sync ();
	return 0;
}

/*----------------------------------------------------------------------------*/
int lcd_clrline(unsigned long line)
{
	int cnt, char_cnt;

	if (lcd == NULL)
		return -ENODEV;

	char_cnt = (lcd_get_width() / VIDEO_FONT_WIDTH) -1;

	lcd_setline(line);
	for(cnt = 0; cnt < char_cnt; cnt ++)
		vidconsole_put_char(lcd->udev_vidcon, 0x20);

	lcd_setline(line);
	lcd_sync ();
	return 0;
}

/*----------------------------------------------------------------------------*/
int lcd_printf(unsigned long x, unsigned long y, unsigned char align,
	const char *fmt, ...)
{
	if (lcd == NULL)
		return -ENODEV;

	va_list args;
	char buf[CONFIG_SYS_PBSIZE];
	int char_cnt, char_length, line_length;
	unsigned long lcd_x;

	va_start(args, fmt);
	char_cnt = vsprintf(buf, fmt, args);
	va_end(args);

	char_length = char_cnt * VIDEO_FONT_WIDTH;
	line_length = lcd_get_width();

	switch(align) {
	/* center */
	case 1:
		if (line_length > char_length)
			lcd_x = (line_length - char_length) / 2;
		else
			lcd_x = x;
		break;
	/* right */
	case 2:
		if (line_length > char_length)
			lcd_x = (line_length - char_length);
		else
			lcd_x = x;
		break;
	/* left */
	default :
		lcd_x = x;
		break;
	}
	lcd_setcur(lcd_x, y * VIDEO_FONT_HEIGHT);
	lcd_putstr(buf);
	return 0;
}

/*----------------------------------------------------------------------------*/
int lcd_setfg(unsigned long r, unsigned long g, unsigned long b)
{
	if (lcd == NULL)
		return -ENODEV;

	lcd->fg_color.r = (r > 255) ? 255 : r;
	lcd->fg_color.g = (g > 255) ? 255 : g;
	lcd->fg_color.b = (b > 255) ? 255 : b;

	return 0;
}

/*----------------------------------------------------------------------------*/
int lcd_setbg(unsigned long r, unsigned long g, unsigned long b)
{
	if (lcd == NULL)
		return -ENODEV;

	lcd->bg_color.r = (r > 255) ? 255 : r;
	lcd->bg_color.g = (g > 255) ? 255 : g;
	lcd->bg_color.b = (b > 255) ? 255 : b;

	return 0;
}

/*----------------------------------------------------------------------------*/
struct lcd_fb_bit *lcd_getfg(void)
{
	return (lcd != NULL) ? &lcd->fg_color : NULL;
}

/*----------------------------------------------------------------------------*/
struct lcd_fb_bit *lcd_getbg(void)
{
	return (lcd != NULL) ? &lcd->bg_color : NULL;
}

/*----------------------------------------------------------------------------*/
int lcd_settransp(unsigned long transp)
{
	if (lcd != NULL)
		lcd->transp = (transp != 0) ? true : false;
	return 0;
}

/*----------------------------------------------------------------------------*/
int lcd_gettransp(void)
{
	return (lcd != NULL) ? lcd->transp : -1;
}

/*----------------------------------------------------------------------------*/
int lcd_setfg_color(const char *color)
{
	if (!strcmp(color ,"red"))
		lcd_setfg(0xff, 0x00, 0x00);
	if (!strcmp(color ,"green"))
		lcd_setfg(0x00, 0xff, 0x00);
	if (!strcmp(color ,"yellow"))
		lcd_setfg(0xff, 0xff, 0x00);
	if (!strcmp(color ,"blue"))
		lcd_setfg(0x00, 0x00, 0xff);
	if (!strcmp(color ,"magenta"))
		lcd_setfg(0xff, 0x00, 0xff);
	if (!strcmp(color ,"cyan"))
		lcd_setfg(0x00, 0xff, 0xff);
	if (!strcmp(color ,"grey"))
		lcd_setfg(0xaa, 0xaa, 0xaa);
	if (!strcmp(color ,"black"))
		lcd_setfg(0x00, 0x00, 0x00);
	if (!strcmp(color ,"white"))
		lcd_setfg(0xff, 0xff, 0xff);
	return 0;
}

/*----------------------------------------------------------------------------*/
int lcd_setbg_color(const char *color)
{
	if (!strcmp(color ,"red"))
		lcd_setbg(0xff, 0x00, 0x00);
	if (!strcmp(color ,"green"))
		lcd_setbg(0x00, 0xff, 0x00);
	if (!strcmp(color ,"yellow"))
		lcd_setbg(0xff, 0xff, 0x00);
	if (!strcmp(color ,"blue"))
		lcd_setbg(0x00, 0x00, 0xff);
	if (!strcmp(color ,"magenta"))
		lcd_setbg(0xff, 0x00, 0xff);
	if (!strcmp(color ,"cyan"))
		lcd_setbg(0x00, 0xff, 0xff);
	if (!strcmp(color ,"grey"))
		lcd_setbg(0xaa, 0xaa, 0xaa);
	if (!strcmp(color ,"black"))
		lcd_setbg(0x00, 0x00, 0x00);
	if (!strcmp(color ,"white"))
		lcd_setbg(0xff, 0xff, 0xff);
	return 0;
}

/*----------------------------------------------------------------------------*/
unsigned long lcd_get_mem(void)
{
	if (lcd_init())
		return get_drm_memory() + (1024 * 768 * 4);
	return get_drm_memory() + (lcd->w * lcd->h * (lcd->bpp / 8));
}

/*----------------------------------------------------------------------------*/
/*
 * Draw the embedded logo on the panel, whatever its resolution.
 *
 * - The logo is an 8bpp gzip'd master image. It is drawn LOGO_EMBEDDED_PCT %
 *   of the panel's short side tall (box-filter downscale), centered, on black.
 * - Panels that are portrait natively (w < h, e.g. 480x854, 720x1280) get the
 *   logo rotated 90 degrees counter-clockwise, the same way dArkOSen's
 *   pre-rotated 720x1280 logo.bmp is drawn, so it is upright in the handheld.
 * - Uses the real framebuffer stride, so nothing is skewed.
 */
static int lcd_show_embedded_logo(void)
{
	struct bmp_image *bmp;
	unsigned long gz_len = LOGO_EMBEDDED_GZ_SIZE;
	const uint8_t *pal, *data, *row, *c;
	uint8_t *buf, *fb, *p;
	int mw, mh, bpp, ncol, pal_off, mstride;
	int fbw, fbh, fbstride, shortside, uw, uh;
	int x, y, i, j, x0, y0, sx0, sx1, sy0, sy1, n;
	unsigned int sb, sg, sr;

	bmp = malloc(LOGO_EMBEDDED_BMP_SIZE);
	if (!bmp) {
		printf("%s : out of memory\n", __func__);
		return -1;
	}

	if (gunzip(bmp, LOGO_EMBEDDED_BMP_SIZE,
		   (unsigned char *)logo_embedded_gz, &gz_len)) {
		printf("%s : gunzip failed\n", __func__);
		free(bmp);
		return -1;
	}

	mw   = get_unaligned_le32(&bmp->header.width);
	mh   = get_unaligned_le32(&bmp->header.height);
	bpp  = get_unaligned_le16(&bmp->header.bit_count);
	ncol = get_unaligned_le32(&bmp->header.colors_used);
	if (!ncol)
		ncol = 256;
	pal_off = 14 + get_unaligned_le32(&bmp->header.size);
	mstride = ALIGN(mw, 4);
	data = (const uint8_t *)bmp + get_unaligned_le32(&bmp->header.data_offset);

	if (bmp->header.signature[0] != 'B' || bmp->header.signature[1] != 'M' ||
	    bpp != 8 || mw <= 0 || mh <= 0 || ncol > 256 ||
	    (int)(data - (const uint8_t *)bmp) < pal_off + ncol * 4 ||
	    (int)(data - (const uint8_t *)bmp) + mstride * mh >
							LOGO_EMBEDDED_BMP_SIZE) {
		printf("%s : bad embedded bmp\n", __func__);
		free(bmp);
		return -1;
	}
	pal = (const uint8_t *)bmp + pal_off;

	/* size on screen */
	fbw = lcd->w;
	fbh = lcd->h;
	shortside = fbw < fbh ? fbw : fbh;
	uh = shortside * LOGO_EMBEDDED_PCT / 100;
	if (uh > mh)
		uh = mh;
	if (uh < 1)
		uh = 1;
	uw = mw * uh / mh;
	if (uw < 1)
		uw = 1;

	buf = malloc(uw * uh * 3);
	if (!buf) {
		printf("%s : out of memory\n", __func__);
		free(bmp);
		return -1;
	}

	/* box-filter downscale of the 8bpp master into upright BGR */
	for (y = 0; y < uh; y++) {
		sy0 = y * mh / uh;
		sy1 = (y + 1) * mh / uh;
		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (x = 0; x < uw; x++) {
			sx0 = x * mw / uw;
			sx1 = (x + 1) * mw / uw;
			if (sx1 <= sx0)
				sx1 = sx0 + 1;
			sb = sg = sr = 0;
			n = 0;
			for (j = sy0; j < sy1; j++) {
				row = data + (mh - 1 - j) * mstride;	/* bottom-up */
				for (i = sx0; i < sx1; i++) {
					c = pal + (row[i] < ncol ? row[i] : 0) * 4;
					sb += c[0];
					sg += c[1];
					sr += c[2];
					n++;
				}
			}
			p = buf + (y * uw + x) * 3;
			p[0] = sb / n;
			p[1] = sg / n;
			p[2] = sr / n;
		}
	}
	free(bmp);

	fb = (uint8_t *)lcd->drm_fb_mem;
	fbstride = fbw * 3;
	memset(fb, 0, lcd->drm_fb_size);

	if (fbw >= fbh) {
		/* landscape or square: upright, centered */
		x0 = (fbw - uw) / 2;
		y0 = (fbh - uh) / 2;
		for (y = 0; y < uh; y++)
			memcpy(fb + (y0 + y) * fbstride + x0 * 3,
			       buf + y * uw * 3, uw * 3);
	} else {
		/* portrait panel: rotate 90 deg CCW, (x,y) -> (y, uw-1-x) */
		x0 = (fbw - uh) / 2;
		y0 = (fbh - uw) / 2;
		for (y = 0; y < uh; y++) {
			for (x = 0; x < uw; x++) {
				p = fb + (y0 + uw - 1 - x) * fbstride +
				    (x0 + y) * 3;
				c = buf + (y * uw + x) * 3;
				p[0] = c[0];
				p[1] = c[1];
				p[2] = c[2];
			}
		}
	}

	free(buf);
	lcd_sync();
	return 0;
}

int lcd_show_logo(void)
{
	if (lcd_init()) {
		printf("%s : lcd init error!\n", __func__);
#if defined(CONFIG_TARGET_ODROIDGO2)
		odroid_drop_errorlog("lcd init fail, check dtb file", 29);
		odroid_alert_leds();
#endif
		return -1;
	}

	/* Logo is built into U-Boot: nothing on the SD card can replace it */
	return lcd_show_embedded_logo();
}

/*----------------------------------------------------------------------------*/
static int do_lcd_cmds(cmd_tbl_t *cmdtp, int flag, int argc,
				char *const argv[])
{
	unsigned long default_rot;
	switch(get_rg351_rev())
	{
		case MODEL_RG351P:
			default_rot = LCD_ROTATE_270;
		break;
			
		case MODEL_RG351MP:
		case MODEL_RG351V:
		default:
			default_rot = LCD_ROTATE_0;
		break;
	
	}

	switch(argc) {
	case 2:
		if (!strcmp("init", argv[1]))
			return lcd_init();
		if (!strcmp("clear", argv[1]))
			return lcd_clear();
		break;
	case 3:
		if (!strcmp("init", argv[1])) {
			unsigned long rot;
			rot = simple_strtoul(argv[2], NULL, 10);
			if (rot > LCD_ROTATE_270)
				rot = default_rot;
			env_set_ulong("lcd_rotate", rot);
			return lcd_init();
		}
		if (!strcmp("putstr", argv[1]))
			return lcd_putstr(argv[2]);
		if (!strcmp("setline", argv[1])) {
			unsigned long line;
			line = simple_strtoul(argv[2], NULL, 10);
			return lcd_setline(line);
		}
		if (!strcmp("clrline", argv[1])) {
			unsigned long line;
			line = simple_strtoul(argv[2], NULL, 10);
			return lcd_clrline(line);
		}
		if (!strcmp("settransp", argv[1])) {
			unsigned long transp;
			transp = simple_strtoul(argv[2], NULL, 10);
			return lcd_settransp(transp);
		}
		if (!strcmp("setfg", argv[1]))
			return lcd_setfg_color(argv[2]);
		if (!strcmp("setbg", argv[1]))
			return lcd_setbg_color(argv[2]);
		break;
	case 4:
		if (!strcmp("setcur", argv[1])) {
			unsigned long x, y;
			x = simple_strtoul(argv[2], NULL, 10);
			y = simple_strtoul(argv[3], NULL, 10);
			return lcd_setcur(x, y);
		}
		break;
	case 5:
		if (!strcmp("setfg", argv[1])) {
			unsigned long r, g, b;
			r = simple_strtoul(argv[2], NULL, 10);
			g = simple_strtoul(argv[3], NULL, 10);
			b = simple_strtoul(argv[4], NULL, 10);
			return lcd_setfg(r, g, b);
		}
		if (!strcmp("setbg", argv[1])) {
			unsigned long r, g, b;
			r = simple_strtoul(argv[2], NULL, 10);
			g = simple_strtoul(argv[3], NULL, 10);
			b = simple_strtoul(argv[4], NULL, 10);
			return lcd_setbg(r, g, b);
		}
		break;
	default:
		break;
	}
	return CMD_RET_USAGE;
}

/*----------------------------------------------------------------------------*/
static char lcd_help_text[] = {
	" - for odroid-goadv lcd control commands\n"
	"init - lcd screen initialize.(default rotate = 0)\n"
	"init <rot(0~3)> - lcd screen initialize & rotate(0-4).\n"
	"clear - lcd screen clear(bg color), move cursor position to 0, 0\n"
	"clrline - lcd line clear(bg color), move cursor position to 0, line\n"
	"putstr <string> - print string on lcd\n"
	"setcur <x> <y> - move cursor position to x, y\n"
	"setline <line> - move cursor position to 0, y(font size)\n"
	"settransp < 1 | 0 > - set background transparent\n"
	"setfg <r> <g> <b> - set foreground color\n"
	"setbg <r> <g> <b> - set background color\n"
	"setfg <color> - set foreground color (color string)\n"
	"setbg <color> - set background color (color string)\n"
	"<color> - red, green, yellow, blue, magenta, cyan, grey, white, black\n"
};

U_BOOT_CMD(
	lcd, 5, 1, do_lcd_cmds,
	"lcd control commands",
	lcd_help_text
);

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
