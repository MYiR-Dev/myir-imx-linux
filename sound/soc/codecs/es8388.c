#include <linux/module.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/regmap.h>
#include "es8388.h"

#define INVALID_GPIO -1

#define ES8388_CODEC_SET_SPK	1
#define ES8388_CODEC_SET_HP	2

#define es8388_DEF_VOL	0x1b

/* codec private data */
struct es8388_priv {
	unsigned int sysclk;
	struct clk *mclk;
	struct regmap *regmap;
	int spk_ctl_gpio;
	int hp_det_gpio;
	struct i2c_client *i2c;

	bool muted;
	bool hp_inserted;
	bool spk_gpio_level;
	bool hp_det_level;

	struct delayed_work pcm_pop_work;
};

static struct es8388_priv *es8388_private;

static int es8388_reset(struct snd_soc_component *codec)
{
	snd_soc_component_write(codec, ES8388_CONTROL1, 0x80);
	return snd_soc_component_write(codec, ES8388_CONTROL1, 0x00);
}

static void pcm_pop_work_events(struct work_struct *work)
{
	struct es8388_priv *es8388 =
		container_of(work, struct es8388_priv, pcm_pop_work.work);
	/* open dac output here */
	regmap_write(es8388->regmap, 0x04, 0x3c);
}

/* dapm controls */
static const char *const es8388_line_texts[] = {
	"Line 1", "Line 2", "PGA"
};

static const unsigned int es8388_line_values[] = {
	0, 1, 3
};

static const char *const es8388_pga_sell[] = { "Line 1L", "Line 2L", "NC", "DifferentialL" };
static const char *const es8388_pga_selr[] = { "Line 1R", "Line 2R", "NC", "DifferentialR" };
static const char *const es8388_lin_sell[] = {"Line 1L", "Line 2L", "NC", "MicL"};
static const char *const es8388_lin_selr[] = {"Line 1R", "Line 2R", "NC", "MicR"};

static const char *const stereo_3d_txt[] = { "No 3D  ", "Level 1", "Level 2", "Level 3",
	"Level 4", "Level 5", "Level 6", "Level 7" };
static const char *const alc_func_txt[] = { "Off", "Right", "Left", "Stereo" };
static const char *const ng_type_txt[] = { "Constant PGA Gain", "Mute ADC Output" };
static const char *const deemph_txt[] = { "None", "32Khz", "44.1Khz", "48Khz" };
static const char *const adcpol_txt[] =	{
	"Normal", "L Invert", "R Invert", "L + R Invert" };
static const char *const es8388_mono_mux[] = {
	"Stereo", "Mono (Left)", "Mono (Right)" };
static const char *const es8388_diff_sel[] = { "Line 1", "Line 2" };

static SOC_ENUM_SINGLE_DECL(es8388_left_dac_enum, ES8388_ADCCONTROL2, 6, es8388_pga_sell);
static SOC_ENUM_SINGLE_DECL(es8388_right_dac_enum, ES8388_ADCCONTROL2, 4, es8388_pga_selr);
static SOC_ENUM_SINGLE_DECL(es8388_diff_enum, ES8388_ADCCONTROL3, 7, es8388_diff_sel);
static SOC_ENUM_SINGLE_DECL(es8388_llin_enum, ES8388_DACCONTROL16, 3, es8388_lin_sell);
static SOC_ENUM_SINGLE_DECL(es8388_rlin_enum, ES8388_DACCONTROL16, 0, es8388_lin_selr);
static SOC_ENUM_SINGLE_DECL(es8388_mono_enum, ES8388_ADCCONTROL3, 3, es8388_mono_mux);

static const struct soc_enum es8388_enum[] = {
	SOC_VALUE_ENUM_SINGLE(ES8388_DACCONTROL16, 3, 7, ARRAY_SIZE(es8388_line_texts),
				es8388_line_texts, es8388_line_values),	/* LLINE */
	SOC_VALUE_ENUM_SINGLE(ES8388_DACCONTROL16, 0, 7, ARRAY_SIZE(es8388_line_texts),
				es8388_line_texts, es8388_line_values),	/* RLINE */
	SOC_VALUE_ENUM_SINGLE(ES8388_ADCCONTROL2, 6, 3, ARRAY_SIZE(es8388_pga_sell),
				es8388_line_texts, es8388_line_values),	/* Left PGA Mux */
	SOC_VALUE_ENUM_SINGLE(ES8388_ADCCONTROL2, 4, 3, ARRAY_SIZE(es8388_pga_sell),
				es8388_line_texts, es8388_line_values),	/* Right PGA Mux */
	SOC_ENUM_SINGLE(ES8388_DACCONTROL7, 2, 8, stereo_3d_txt),	/* stereo-3d */
	SOC_ENUM_SINGLE(ES8388_ADCCONTROL10, 6, 4, alc_func_txt),	/* alc func */
	SOC_ENUM_SINGLE(ES8388_ADCCONTROL14, 1, 2, ng_type_txt),	/* noise gate type */
	SOC_ENUM_SINGLE(ES8388_DACCONTROL6, 6, 4, deemph_txt),	/* Playback De-emphasis */
	SOC_ENUM_SINGLE(ES8388_ADCCONTROL6, 6, 4, adcpol_txt),
	SOC_ENUM_SINGLE(ES8388_ADCCONTROL3, 3, 3, es8388_mono_mux),
	SOC_ENUM_SINGLE(ES8388_ADCCONTROL3, 7, 2, es8388_diff_sel),
};

static const DECLARE_TLV_DB_SCALE(pga_tlv, 0, 300, 0);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -9600, 50, 1);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -9600, 50, 1);
static const DECLARE_TLV_DB_SCALE(out_tlv, -4500, 150, 0);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, 0, 300, 0);
static const DECLARE_TLV_DB_SCALE(bypass_tlv2, -15, 300, 0);

static const struct snd_kcontrol_new
	es8388_left_dac_mux_controls = SOC_DAPM_ENUM("Route", es8388_left_dac_enum);
static const struct snd_kcontrol_new
	es8388_right_dac_mux_controls = SOC_DAPM_ENUM("Route", es8388_right_dac_enum);
static const struct snd_kcontrol_new
	es8388_diffmux_controls = SOC_DAPM_ENUM("Route2", es8388_diff_enum);

static const struct snd_kcontrol_new es8388_snd_controls[] = {
	SOC_ENUM("3D Mode", es8388_enum[4]),
	SOC_SINGLE("ALC Capture Target Volume", ES8388_ADCCONTROL11, 4, 15, 0),
	SOC_SINGLE("ALC Capture Max PGA", ES8388_ADCCONTROL10, 3, 7, 0),
	SOC_SINGLE("ALC Capture Min PGA", ES8388_ADCCONTROL10, 0, 7, 0),
	SOC_ENUM("ALC Capture Function", es8388_enum[5]),
	SOC_SINGLE("ALC Capture ZC Switch", ES8388_ADCCONTROL13, 6, 1, 0),
	SOC_SINGLE("ALC Capture Hold Time", ES8388_ADCCONTROL11, 0, 15, 0),
	SOC_SINGLE("ALC Capture Decay Time", ES8388_ADCCONTROL12, 4, 15, 0),
	SOC_SINGLE("ALC Capture Attack Time", ES8388_ADCCONTROL12, 0, 15, 0),
	SOC_SINGLE("ALC Capture NG Threshold", ES8388_ADCCONTROL14, 3, 31, 0),
	SOC_ENUM("ALC Capture NG Type", es8388_enum[6]),
	SOC_SINGLE("ALC Capture NG Switch", ES8388_ADCCONTROL14, 0, 1, 0),
	SOC_SINGLE("ZC Timeout Switch", ES8388_ADCCONTROL13, 6, 1, 0),
	SOC_DOUBLE_R_TLV("Capture Digital Volume", ES8388_ADCCONTROL8,
			 ES8388_ADCCONTROL9, 0, 192, 1, adc_tlv),
	SOC_SINGLE("Capture Mute", ES8388_ADCCONTROL7, 2, 1, 0),
	SOC_SINGLE_TLV("Left Channel Capture Volume", ES8388_ADCCONTROL1, 4, 8,
		       0, bypass_tlv),
	SOC_SINGLE_TLV("Right Channel Capture Volume", ES8388_ADCCONTROL1, 0,
		       8, 0, bypass_tlv),
	SOC_ENUM("Playback De-emphasis", es8388_enum[7]),
	SOC_ENUM("Capture Polarity", es8388_enum[8]),
	SOC_DOUBLE_R_TLV("PCM Volume", ES8388_DACCONTROL4, ES8388_DACCONTROL5,
			 0, 192, 1, dac_tlv),
	SOC_SINGLE_TLV("Left Mixer Left Bypass Volume", ES8388_DACCONTROL17, 3,
		       7, 1, bypass_tlv2),
	SOC_SINGLE_TLV("Right Mixer Right Bypass Volume", ES8388_DACCONTROL20,
		       3, 7, 1, bypass_tlv2),
	SOC_DOUBLE_R_TLV("Output 1 Playback Volume", ES8388_DACCONTROL24,
			 ES8388_DACCONTROL25, 0, 33, 0, out_tlv),
	SOC_DOUBLE_R_TLV("Output 2 Playback Volume", ES8388_DACCONTROL26,
			 ES8388_DACCONTROL27, 0, 33, 0, out_tlv),
};

static const struct snd_kcontrol_new es8388_left_line_controls =
SOC_DAPM_ENUM("LLIN Mux", es8388_llin_enum);

static const struct snd_kcontrol_new es8388_right_line_controls =
SOC_DAPM_ENUM("RLIN Mux", es8388_rlin_enum);
/* Mono ADC Mux */
static const struct snd_kcontrol_new es8388_monomux_controls =
SOC_DAPM_ENUM("Mono Mux", es8388_mono_enum);

/* Right PGA Mux */
static const struct snd_kcontrol_new es8388_right_pga_controls =
SOC_DAPM_ENUM("Route", es8388_enum[3]);

/* Left Mixer */
static const struct snd_kcontrol_new es8388_left_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left Playback Switch", ES8388_DACCONTROL17, 7, 1, 0),
	SOC_DAPM_SINGLE("Left Bypass Switch", ES8388_DACCONTROL17, 6, 1, 0),
};

/* Right Mixer */
static const struct snd_kcontrol_new es8388_right_mixer_controls[] = {
	SOC_DAPM_SINGLE("Right Playback Switch", ES8388_DACCONTROL20, 7, 1, 0),
	SOC_DAPM_SINGLE("Right Bypass Switch", ES8388_DACCONTROL20, 6, 1, 0),
};

static const struct snd_soc_dapm_widget es8388_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("LINPUT2"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT2"),
	SND_SOC_DAPM_MUX("Left PGA Mux", SND_SOC_NOPM, 0, 0,
			 &es8388_left_dac_mux_controls),
	SND_SOC_DAPM_MUX("Right PGA Mux", SND_SOC_NOPM, 0, 0,
			 &es8388_right_dac_mux_controls),
	SND_SOC_DAPM_MICBIAS("Mic Bias", ES8388_ADCPOWER, 3, 1),

	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0,
			 &es8388_diffmux_controls),

	SND_SOC_DAPM_MUX("Left ADC Mux", SND_SOC_NOPM, 0, 0,
			 &es8388_monomux_controls),
	SND_SOC_DAPM_MUX("Right ADC Mux", SND_SOC_NOPM, 0, 0,
			 &es8388_monomux_controls),

	SND_SOC_DAPM_MUX("Left Line Mux", SND_SOC_NOPM, 0, 0,
			 &es8388_left_line_controls),
	SND_SOC_DAPM_MUX("Right Line Mux", SND_SOC_NOPM, 0, 0,
			 &es8388_right_line_controls),
	
	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", ES8388_ADCPOWER, 4, 1),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", ES8388_ADCPOWER, 5, 1),

	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
			   &es8388_left_mixer_controls[0],
			   ARRAY_SIZE(es8388_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
			   &es8388_right_mixer_controls[0],
			   ARRAY_SIZE(es8388_right_mixer_controls)),
	SND_SOC_DAPM_PGA("Right ADC Power", ES8388_ADCPOWER, 6, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Left ADC Power", ES8388_ADCPOWER, 7, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("LAMP", ES8388_ADCCONTROL1, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RAMP", ES8388_ADCCONTROL1, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("ROUT2"),
	SND_SOC_DAPM_OUTPUT("VREF"),
};

static const struct snd_soc_dapm_route audio_map[] = {

	{"Left PGA Mux", "Line 1L", "LINPUT1"},
	{"Left PGA Mux", "Line 2L", "LINPUT2"},
	{"Left PGA Mux", "DifferentialL", "Differential Mux"},

	{"Right PGA Mux", "Line 1R", "RINPUT1"},
	{"Right PGA Mux", "Line 2R", "RINPUT2"},
	{"Right PGA Mux", "DifferentialR", "Differential Mux"},

	{"Differential Mux", "Line 1", "LINPUT1"},
	{"Differential Mux", "Line 1", "RINPUT1"},
	{"Differential Mux", "Line 2", "LINPUT2"},
	{"Differential Mux", "Line 2", "RINPUT2"},

	{"Left ADC Mux", "Stereo", "Right PGA Mux"},
	{"Left ADC Mux", "Stereo", "Left PGA Mux"},
	{"Left ADC Mux", "Mono (Left)", "Left PGA Mux"},

	{"Right ADC Mux", "Stereo", "Left PGA Mux"},
	{"Right ADC Mux", "Stereo", "Right PGA Mux"},
	{"Right ADC Mux", "Mono (Right)", "Right PGA Mux"},

	{"Left ADC Power", NULL, "Left ADC Mux"},
	{"Right ADC Power", NULL, "Right ADC Mux"},
	{"Left ADC", NULL, "Left ADC Power"},
	{"Right ADC", NULL, "Right ADC Power"},

	{"Left Line Mux", "Line 1L", "LINPUT1"},
	{"Left Line Mux", "Line 2L", "LINPUT2"},
	{"Left Line Mux", "MicL", "Left PGA Mux"},

	{"Right Line Mux", "Line 1R", "RINPUT1"},
	{"Right Line Mux", "Line 2R", "RINPUT2"},
	{"Right Line Mux", "MicR", "Right PGA Mux"},

	{"Left Mixer", "Left Playback Switch", "Left DAC"},
	{"Left Mixer", "Left Bypass Switch", "Left Line Mux"},

	{"Right Mixer", "Right Playback Switch", "Right DAC"},
	{"Right Mixer", "Right Bypass Switch", "Right Line Mux"},

	{"Left Out 1", NULL, "Left Mixer"},
	{"LOUT1", NULL, "Left Out 1"},
	{"Right Out 1", NULL, "Right Mixer"},
	{"ROUT1", NULL, "Right Out 1"},

	{"Left Out 2", NULL, "Left Mixer"},
	{"LOUT2", NULL, "Left Out 2"},
	{"Right Out 2", NULL, "Right Mixer"},
	{"ROUT2", NULL, "Right Out 2"},

};

struct _coeff_div {
	u32 mclk;
	u32 rate;
	u16 fs;
	u8 sr:4;
	u8 usb:1;
};

/* codec hifi mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 8k */
	{12288000, 8000, 1536, 0xa, 0x0},
	{11289600, 8000, 1408, 0x9, 0x0},
	{18432000, 8000, 2304, 0xc, 0x0},
	{16934400, 8000, 2112, 0xb, 0x0},
	{12000000, 8000, 1500, 0xb, 0x1},

	/* 11.025k */
	{11289600, 11025, 1024, 0x7, 0x0},
	{16934400, 11025, 1536, 0xa, 0x0},
	{12000000, 11025, 1088, 0x9, 0x1},

	/* 16k */
	{12288000, 16000, 768, 0x6, 0x0},
	{18432000, 16000, 1152, 0x8, 0x0},
	{12000000, 16000, 750, 0x7, 0x1},

	/* 22.05k */
	{11289600, 22050, 512, 0x4, 0x0},
	{16934400, 22050, 768, 0x6, 0x0},
	{12000000, 22050, 544, 0x6, 0x1},

	/* 32k */
	{12288000, 32000, 384, 0x3, 0x0},
	{18432000, 32000, 576, 0x5, 0x0},
	{12000000, 32000, 375, 0x4, 0x1},

	/* 44.1k */
	{11289600, 44100, 256, 0x2, 0x0},
	{16934400, 44100, 384, 0x3, 0x0},
	{12000000, 44100, 272, 0x3, 0x1},

	/* 48k */
	{12288000, 48000, 256, 0x2, 0x0},
	{18432000, 48000, 384, 0x3, 0x0},
	{12000000, 48000, 250, 0x2, 0x1},

	/* 88.2k */
	{11289600, 88200, 128, 0x0, 0x0},
	{16934400, 88200, 192, 0x1, 0x0},
	{12000000, 88200, 136, 0x1, 0x1},

	/* 96k */
	{12288000, 96000, 128, 0x0, 0x0},
	{18432000, 96000, 192, 0x1, 0x0},
	{12000000, 96000, 125, 0x0, 0x1},
};

static inline int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}

	return -EINVAL;
}

/*
 * Note that this should be called from init rather than from hw_params.
 */
static int es8388_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *codec = codec_dai->component;
	struct es8388_priv *es8388 = snd_soc_component_get_drvdata(codec);

	es8388->sysclk = freq;

	return 0;
}

static int es8388_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *codec = codec_dai->component;
	u8 iface = 0;
	u8 adciface = 0;
	u8 daciface = 0;

	iface = snd_soc_component_read(codec, ES8388_IFACE);
	adciface = snd_soc_component_read(codec, ES8388_ADC_IFACE);
	daciface = snd_soc_component_read(codec, ES8388_DAC_IFACE);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:	/* MASTER MODE */
		iface |= 0x80;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:	/* SLAVE MODE */
		iface &= 0x7F;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		adciface &= 0xFC;
		daciface &= 0xF9;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		return -EINVAL;
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	case SND_SOC_DAIFMT_DSP_A:
		break;
	case SND_SOC_DAIFMT_DSP_B:
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		iface &= 0xDF;
		adciface &= 0xDF;
		daciface &= 0xBF;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x20;
		adciface |= 0x20;
		daciface |= 0x40;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x20;
		adciface &= 0xDF;
		daciface &= 0xBF;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface &= 0xDF;
		adciface |= 0x20;
		daciface |= 0x40;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(codec, ES8388_IFACE, iface);
	snd_soc_component_write(codec, ES8388_ADC_IFACE, adciface);
	snd_soc_component_write(codec, ES8388_DAC_IFACE, daciface);

	return 0;
}

static int es8388_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	return 0;
}

static int es8388_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *codec = dai->component;
	struct es8388_priv *es8388 = snd_soc_component_get_drvdata(codec);
	u16 srate = snd_soc_component_read(codec, ES8388_IFACE) & 0x80;
	u16 adciface = snd_soc_component_read(codec, ES8388_ADC_IFACE) & 0xE3;
	u16 daciface = snd_soc_component_read(codec, ES8388_DAC_IFACE) & 0xC7;
	int coeff;

	/* If the es8388->sysclk is a fixed value, for example 12.288M,
	 * set es8388->sysclk = 12288000;
	 * else if es8388->sysclk is some value times lrck, for example 128Fs,
	 * set es8388->sysclk = 128 * params_rate(params);
	 */
	coeff = get_coeff(es8388->sysclk, params_rate(params));
	if (coeff < 0) {
		coeff = get_coeff(es8388->sysclk / 2, params_rate(params));
		srate |= 0x40;
	}
	if (coeff < 0) {
		dev_err(codec->dev,
			"Unable to configure sample rate %dHz with %dHz MCLK\n",
			params_rate(params), es8388->sysclk);
		return coeff;
	}

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adciface |= 0x000C;
		daciface |= 0x0018;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adciface |= 0x0004;
		daciface |= 0x0008;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adciface |= 0x0010;
		daciface |= 0x0020;
		break;
	}

	/* set iface & srate */
	snd_soc_component_write(codec, ES8388_DAC_IFACE, daciface);
	snd_soc_component_write(codec, ES8388_ADC_IFACE, adciface);

	if (coeff >= 0) {
		snd_soc_component_write(codec, ES8388_IFACE, srate);
		snd_soc_component_write(codec, ES8388_ADCCONTROL5,
			      coeff_div[coeff].sr | (coeff_div[coeff].usb) << 4);
		snd_soc_component_write(codec, ES8388_DACCONTROL2,
			      coeff_div[coeff].sr | (coeff_div[coeff].usb) << 4);
	}

	snd_soc_component_write(codec, 0x01, 0x60);
	snd_soc_component_write(codec, 0x02, 0xF3);
	snd_soc_component_write(codec, 0x02, 0xF0);
	snd_soc_component_write(codec, 0x2B, 0x80);
	snd_soc_component_write(codec, 0x00, 0x36);
	snd_soc_component_write(codec, 0x08, 0x00);
	snd_soc_component_write(codec, 0x04, 0x00);
	snd_soc_component_write(codec, 0x09, 0x88);
	snd_soc_component_write(codec, 0x0A, 0xF8);
	snd_soc_component_write(codec, 0x0B, 0x9a);
	snd_soc_component_write(codec, 0x0C, 0x4C);
	snd_soc_component_write(codec, 0x0D, 0x02);
	snd_soc_component_write(codec, 0x10, 0x00);
	snd_soc_component_write(codec, 0x11, 0x00);
	snd_soc_component_write(codec, 0x12, 0x00);
	snd_soc_component_write(codec, 0x13, 0xc0);
	snd_soc_component_write(codec, 0x14, 0x05);
	snd_soc_component_write(codec, 0x15, 0x06);
	snd_soc_component_write(codec, 0x16, 0x52);
	snd_soc_component_write(codec, 0x17, 0x18);
	snd_soc_component_write(codec, 0x18, 0x02);
	snd_soc_component_write(codec, 0x1A, 0x00);
	snd_soc_component_write(codec, 0x1B, 0x00);
	snd_soc_component_write(codec, 0x1D, 0x20);
	snd_soc_component_write(codec, 0x27, 0xB8);
	snd_soc_component_write(codec, 0x2A, 0xB8);
	snd_soc_component_write(codec, 0x35, 0xA0);
	snd_soc_component_write(codec, 0x37, 0xD0);
	snd_soc_component_write(codec, 0x39, 0xD0);
	usleep_range(18000, 20000);
	snd_soc_component_write(codec, 0x2E, 0x1E);
	snd_soc_component_write(codec, 0x2F, 0x1E);
	snd_soc_component_write(codec, 0x30, 0x00);
	snd_soc_component_write(codec, 0x31, 0x00);
	snd_soc_component_write(codec, 0x03, 0x09);
	snd_soc_component_write(codec, 0x02, 0x00);
	snd_soc_component_write(codec, 0x05, 0xE8);
	snd_soc_component_write(codec, 0x06, 0xC3);
	usleep_range(18000, 20000);
	/* open dac output later */
	// snd_soc_component_write(codec, 0x04, 0xC0);

	return 0;
}

static int es8388_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *codec = dai->component;
	struct es8388_priv *es8388 = snd_soc_component_get_drvdata(codec);

	es8388->muted = mute;
	if (mute) {
		snd_soc_component_write(codec, 0x04, 0xC0);
		snd_soc_component_write(codec, ES8388_DACCONTROL3, 0x06);
	} else {
		snd_soc_component_write(codec, 0x04, 0xF0);
		snd_soc_component_write(codec, ES8388_DACCONTROL3, 0x02);
	}
	return 0;
}

static int es8388_set_bias_level(struct snd_soc_component *codec,
				 enum snd_soc_bias_level level)
{
	struct es8388_priv *es8388 = snd_soc_component_get_drvdata(codec);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		dev_dbg(codec->dev, "%s on\n", __func__);
		break;
	case SND_SOC_BIAS_PREPARE:
		dev_dbg(codec->dev, "%s prepare\n", __func__);
		if (IS_ERR(es8388->mclk))
			break;
		if (snd_soc_component_get_bias_level(codec) == SND_SOC_BIAS_ON) {
			clk_disable_unprepare(es8388->mclk);
		} else {
			ret = clk_prepare_enable(es8388->mclk);
			if (ret)
				return ret;
		}
		snd_soc_component_write(codec, ES8388_ANAVOLMANAG, 0x7C);
		snd_soc_component_write(codec, ES8388_CHIPLOPOW1, 0x00);
		snd_soc_component_write(codec, ES8388_CHIPLOPOW2, 0x00);
		snd_soc_component_write(codec, ES8388_CHIPPOWER, 0x00);
		snd_soc_component_write(codec, ES8388_ADCPOWER, 0x59);
		break;
	case SND_SOC_BIAS_STANDBY:
		dev_dbg(codec->dev, "%s standby\n", __func__);
		snd_soc_component_write(codec, ES8388_ANAVOLMANAG, 0x7C);
		snd_soc_component_write(codec, ES8388_CHIPLOPOW1, 0x00);
		snd_soc_component_write(codec, ES8388_CHIPLOPOW2, 0x00);
		snd_soc_component_write(codec, ES8388_CHIPPOWER, 0x00);
		snd_soc_component_write(codec, ES8388_ADCPOWER, 0x59);
		break;
	case SND_SOC_BIAS_OFF:
		if (es8388->mclk)
			clk_disable_unprepare(es8388->mclk);
		dev_dbg(codec->dev, "%s off\n", __func__);
		snd_soc_component_write(codec, ES8388_ADCPOWER, 0xFF);
		//snd_soc_component_write(codec, ES8388_DACPOWER, 0xC0);
		snd_soc_component_write(codec, ES8388_CHIPLOPOW1, 0xFF);
		snd_soc_component_write(codec, ES8388_CHIPLOPOW2, 0xFF);
		snd_soc_component_write(codec, ES8388_CHIPPOWER, 0xFF);
		snd_soc_component_write(codec, ES8388_ANAVOLMANAG, 0x7B);
		break;
	}
		return 0;
}

#define es8388_RATES SNDRV_PCM_RATE_8000_96000

#define es8388_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FORMAT_S32_LE)

static const struct snd_soc_dai_ops es8388_ops = {
	.startup = es8388_pcm_startup,
	.hw_params = es8388_pcm_hw_params,
	.set_fmt = es8388_set_dai_fmt,
	.set_sysclk = es8388_set_dai_sysclk,
	.mute_stream = es8388_mute,
};

static struct snd_soc_dai_driver es8388_dai = {
	.name = "ES8388 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = es8388_RATES,
		.formats = es8388_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = es8388_RATES,
		.formats = es8388_FORMATS,
	},
	.ops = &es8388_ops,
	.symmetric_rate = 1,
};

static int es8388_suspend(struct snd_soc_component *codec)
{
	snd_soc_component_write(codec, 0x19, 0x06);
	snd_soc_component_write(codec, 0x30, 0x00);
	snd_soc_component_write(codec, 0x31, 0x00);
	snd_soc_component_write(codec, ES8388_ADCPOWER, 0xFF);
	snd_soc_component_write(codec, ES8388_DACPOWER, 0xc0);
	snd_soc_component_write(codec, ES8388_CHIPPOWER, 0xF3);
	snd_soc_component_write(codec, 0x00, 0x00);
	snd_soc_component_write(codec, 0x01, 0x58);
	snd_soc_component_write(codec, 0x2b, 0x9c);
	usleep_range(18000, 20000);
	return 0;
}

static int es8388_resume(struct snd_soc_component *codec)
{
	snd_soc_component_write(codec, 0x2b, 0x80);
	snd_soc_component_write(codec, 0x01, 0x50);
	snd_soc_component_write(codec, 0x00, 0x32);
	snd_soc_component_write(codec, ES8388_CHIPPOWER, 0x00);
	snd_soc_component_write(codec, ES8388_DACPOWER, 0x3c);
	snd_soc_component_write(codec, ES8388_ADCPOWER, 0x59);
	snd_soc_component_write(codec, 0x31, es8388_DEF_VOL);
	snd_soc_component_write(codec, 0x30, es8388_DEF_VOL);
	snd_soc_component_write(codec, 0x19, 0x02);
	return 0;
}

static struct snd_soc_component *es8388_codec;
static int es8388_probe(struct snd_soc_component *codec)
{
	int ret = 0;

	if (codec == NULL) {
		dev_err(codec->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	es8388_codec = codec;
	ret = es8388_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		return ret;
	}

	es8388_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

static const struct regmap_config es8388_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0x4f,

	.cache_type = REGCACHE_RBTREE,
};

static const struct snd_soc_component_driver soc_codec_dev_es8388 = {
	.probe = es8388_probe,
	.suspend = es8388_suspend,
	.resume = es8388_resume,
	.set_bias_level = es8388_set_bias_level,

	.dapm_widgets		= es8388_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(es8388_dapm_widgets),
	.dapm_routes		= audio_map,
	.num_dapm_routes	= ARRAY_SIZE(audio_map),
	.controls		= es8388_snd_controls,
	.num_controls		= ARRAY_SIZE(es8388_snd_controls),

};

static int es8388_i2c_probe(struct i2c_client *i2c)
{
	struct es8388_priv *es8388;
	int ret = -1;
	//enum of_gpio_flags flags;
	//char reg;

	es8388 = devm_kzalloc(&i2c->dev, sizeof(struct es8388_priv), GFP_KERNEL);
	if (es8388 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, es8388);
	es8388->i2c = i2c;

	es8388_private = es8388;
	/* Enable the following code if there is no mclk.
	 * a clock named "mclk" need to be defined in the dts (see sample dts)
	 *
	 * No need to enable the following code to get mclk if:
	 * 1. sclk/bclk is used as mclk
	 * 2. mclk is controled by soc I2S
	 */
#if 0
	es8388->mclk = devm_clk_get(&i2c->dev, "mclk");
	if (IS_ERR(es8388->mclk)) {
		dev_err(&i2c->dev, "%s mclk is missing or invalid\n", __func__);
		return PTR_ERR(es8388->mclk);
	}
	ret = clk_prepare_enable(es8388->mclk);
	if (ret)
		return ret;
#endif
	es8388->regmap = devm_regmap_init_i2c(i2c, &es8388_regmap_config);
	INIT_DELAYED_WORK(&es8388->pcm_pop_work,
			  pcm_pop_work_events);
	ret = snd_soc_register_component(&i2c->dev,
					&soc_codec_dev_es8388,
					&es8388_dai, 1);
	return ret;
}

static void es8388_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_component(&client->dev);
}

static const struct i2c_device_id es8388_i2c_id[] = {
	{"es8388", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, es8388_i2c_id);

static void es8388_i2c_shutdown(struct i2c_client *client)
{
	snd_soc_component_write(es8388_codec, ES8388_CONTROL2, 0x58);
	snd_soc_component_write(es8388_codec, ES8388_CONTROL1, 0x32);
	snd_soc_component_write(es8388_codec, ES8388_CHIPPOWER, 0xf3);
	snd_soc_component_write(es8388_codec, ES8388_DACPOWER, 0xc0);
	mdelay(50);
	snd_soc_component_write(es8388_codec, ES8388_DACCONTROL26, 0x00);
	snd_soc_component_write(es8388_codec, ES8388_DACCONTROL27, 0x00);
	mdelay(50);
	snd_soc_component_write(es8388_codec, ES8388_CONTROL1, 0x30);
	snd_soc_component_write(es8388_codec, ES8388_CONTROL1, 0x34);
}

static const struct of_device_id es8388_of_match[] = {
	{ .compatible = "everest,es8388", },
	{ }
};
MODULE_DEVICE_TABLE(of, es8388_of_match);

static struct i2c_driver es8388_i2c_driver = {
	.driver = {
		.name = "ES8388",
		.of_match_table = of_match_ptr(es8388_of_match),
		},
	.shutdown = es8388_i2c_shutdown,
	.probe = es8388_i2c_probe,
	.remove = es8388_i2c_remove,
	.id_table = es8388_i2c_id,
};
module_i2c_driver(es8388_i2c_driver);

MODULE_DESCRIPTION("ASoC es8388 driver");
MODULE_AUTHOR("Mark Brown <will@everset-semi.com>");
MODULE_LICENSE("GPL");
