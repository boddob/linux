#ifndef _MSM8916_WCD_H_
#define _MSM8916_WCD_H_

#define MBHC_MAX_BUTTONS	(5)
struct msm8916_wcd_mbhc_data {
	/* Voltage threshold when internal current source of 100uA is used */
	int vref_btn_cs[MBHC_MAX_BUTTONS];
	/* Voltage threshold when microphone bias is ON */
	int vref_btn_micb[MBHC_MAX_BUTTONS];
};

#endif /* _MSM8916_WCD_H_ */
