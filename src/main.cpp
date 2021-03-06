/*
  Copyright 2020 Bga <bga.email@gmail.com>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

//#define F_CPU 8000000UL

#include <stdint.h>
#include <string.h>
#include <stm8s.h>
#include <intrinsics.h>
#include <eeprom.h>

#include <!cpp/bitManipulations.h>
#include <!cpp/Binary_values_8_bit.h>
#include <!cpp/RunningAvg.h>
#include <!cpp/newKeywords.h>

#include "_7SegmentsFont.h"

using namespace Stm8Hal;

enum {
	clockDivider = 0,
	ticksCountPerSAprox = 1600UL,

	TIM4_prescaler = 7,
	TIM4_arr = F_CPU / (1UL << clockDivider ) / (1UL << TIM4_prescaler) / ticksCountPerSAprox,

	ticksCountPerSReal = F_CPU / (1UL << clockDivider ) / (1UL << TIM4_prescaler) / TIM4_arr
};

static_assert_lt(0, TIM4_arr);
static_assert_lte(TIM4_arr, 255);

#define msToTicksCount(msArg) (ticksCountPerSReal * (msArg) / 1000UL)

void Timer_init() {
	TIM4_PSCR = TIM4_prescaler;

	TIM4_ARR = TIM4_arr;

	setBit(TIM4_IER, TIM4_IER_UIE); // Enable Update Interrupt
	setBit(TIM4_CR1, TIM4_CR1_CEN); // Enable TIM4
}

enum { adcMaxBufferSize = 32 };

enum {
	//# fetch ADC - 1600Hz / 16 = 100Hz
	adcFetchSpeedPrescaler = 4,

	//# display ADC - 1600Hz / 16 / 32 = ~3Hz
	// adcDisplaySpeedPrescaler = 9,
	AdcUser_maxValue = 1000,
};

volatile GPIO_TypeDef* const digit2CathodeGpioPort = (GPIO_TypeDef*)PD_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit1CathodeGpioPort = (GPIO_TypeDef*)PD_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit0CathodeGpioPort = (GPIO_TypeDef*)PD_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit5CathodeGpioPort = (GPIO_TypeDef*)PA_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit4CathodeGpioPort = (GPIO_TypeDef*)PB_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit3CathodeGpioPort = (GPIO_TypeDef*)PB_BASE_ADDRESS;

enum {
	digit2CathodeGpioPortBit = 5,
	digit1CathodeGpioPortBit = 6,
	digit0CathodeGpioPortBit = 4,
	digit5CathodeGpioPortBit = 1,
	digit4CathodeGpioPortBit = 4,
	digit3CathodeGpioPortBit = 5,
};

volatile GPIO_TypeDef* const digit0AnodeGpioPort = (GPIO_TypeDef*)PC_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit1AnodeGpioPort = (GPIO_TypeDef*)PC_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit2AnodeGpioPort = (GPIO_TypeDef*)PC_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit3AnodeGpioPort = (GPIO_TypeDef*)PC_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit4AnodeGpioPort = (GPIO_TypeDef*)PA_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit5AnodeGpioPort = (GPIO_TypeDef*)PA_BASE_ADDRESS;
volatile GPIO_TypeDef* const digit6AnodeGpioPort = (GPIO_TypeDef*)PD_BASE_ADDRESS;
volatile GPIO_TypeDef* const digitDotAnodeGpioPort = (GPIO_TypeDef*)PC_BASE_ADDRESS;

enum {
	digit0AnodeGpioPortBit = 6,
	digit1AnodeGpioPortBit = 7,
	digit2AnodeGpioPortBit = 3,
	digit3AnodeGpioPortBit = 4,
	digit4AnodeGpioPortBit = 3,
	digit5AnodeGpioPortBit = 2,
	digit6AnodeGpioAndSwimPortBit = 1,
	digitDotAnodeGpioPortBit = 5,
};

enum {
	voltageAdcChannelNo = 4,
	voltageAdcPortD = 3,
	currentAdcChannelNo = 3,
	currentAdcPortD = 2
};

#if 1
FU16 divmod10(FU16* in) {
	FU16 div = *in  / 10;
	FU16 mod = *in % 10;

	*in = div;
	return mod;
}
#else
FU16 divmod10(FU16& in) {
  // q = in * 0.8;
  FU16 q = (in >> 1) + (in >> 2);
  q = q + (q >> 4);
  q = q + (q >> 8);
//  q = q + (q >> 16);  // not needed for 16 bit version

  // q = q / 8;  ==> q =  in *0.1;
  q = q >> 3;

  // determine error
  FU16 r = in - ((q << 3) + (q << 1));   // r = in - q*10;
  FU16 div = q;
  FU16 mod = ((r > 9) ? ++div, r - 10 : r);

  in = div;

  return mod;
}
#endif // 1

FU16 get10Power(FU16 x, FU16 max) {
	FU16 ret = 1;
	while(ret <= max && 1000 <= x) {
		x /= 10;
		ret *= 10;
	}

	return ret;
}

void ADC_init() {
	/* right-align data */
	setBit(ADC1_CR2, ADC1_CR2_ALIGN);

	//# ADC clock = fMasterClock / 18
	setBitMaskedValues(ADC1_CR1, 4, 0x07, 7);

	/* wake ADC from power down */
	setBit(ADC1_CR1, ADC1_CR1_ADON);
}

void ADC_initChannel(FU8 channelNo) {
	if(channelNo < 8) {
		setBit(ADC2_TDRL, channelNo);
	}
	else {
		setBit(ADC2_TDRH, channelNo - 8);
	}
}

void ADC_setChannel(FU8 channelNo) {
	setBitMaskedValues(ADC1_CSR, 0, 0x0F, channelNo);
}

void ADC_readStart() {
	setBit(ADC1_CR1, ADC1_CR1_ADON);
}

FU16 ADC_read() {
	while (!(ADC1_CSR & _BV(ADC1_CSR_EOC)));
	U8 adcL = ADC1_DRL;
	U8 adcH = ADC1_DRH;
	clearBit(ADC1_CSR, ADC1_CSR_EOC);
	return (adcL | (adcH << 8));
}

FU16 ADC_readSync(FU8 channelNo) {
	ADC_setChannel(channelNo);
	ADC_readStart();
	return ADC_read();
}

Bool AdcUser_isOverflow(FU16 v) {
	return AdcUser_maxValue <= v;
}

FU16 ADCUser_transformOverflow(FU16 v) {
	return (AdcUser_isOverflow(v)) ? FU16(-1) : v;
}

struct Display {
	FU8 displayChars[3 + 3];
	FU8 currentDisplayIndex;

	void init() {
		struct F {
			static inline void initCathode(volatile GPIO_TypeDef* port, U8 bit) {
				setBit(port->DDR, bit);
				setBit(port->ODR, bit);
			}
			static inline void initAnode(volatile GPIO_TypeDef* port, U8 bit) {
				setBit(port->DDR, bit);
				setBit(port->CR1, bit);
			}
		};

		F::initCathode(digit0CathodeGpioPort, digit0CathodeGpioPortBit);
		F::initCathode(digit1CathodeGpioPort, digit1CathodeGpioPortBit);
		F::initCathode(digit2CathodeGpioPort, digit2CathodeGpioPortBit);
		F::initCathode(digit3CathodeGpioPort, digit3CathodeGpioPortBit);
		F::initCathode(digit4CathodeGpioPort, digit4CathodeGpioPortBit);
		F::initCathode(digit5CathodeGpioPort, digit5CathodeGpioPortBit);

		F::initAnode(digit0AnodeGpioPort, digit0AnodeGpioPortBit);
		F::initAnode(digit1AnodeGpioPort, digit1AnodeGpioPortBit);
		F::initAnode(digit2AnodeGpioPort, digit2AnodeGpioPortBit);
		F::initAnode(digit3AnodeGpioPort, digit3AnodeGpioPortBit);
		F::initAnode(digit4AnodeGpioPort, digit4AnodeGpioPortBit);
		F::initAnode(digit5AnodeGpioPort, digit5AnodeGpioPortBit);
		#if NDEBUG
			F::initAnode(digit6AnodeGpioPort, digit6AnodeGpioAndSwimPortBit);
		#endif // NDEBUG
		F::initAnode(digitDotAnodeGpioPort, digitDotAnodeGpioPortBit);
	}

	void turnOffDisplay() {
		//# do not touch SWIM during debug
		setBitValue(digit0CathodeGpioPort->ODR, digit0CathodeGpioPortBit, 1);
		setBitValue(digit1CathodeGpioPort->ODR, digit1CathodeGpioPortBit, 1);
		setBitValue(digit2CathodeGpioPort->ODR, digit2CathodeGpioPortBit, 1);
		setBitValue(digit5CathodeGpioPort->ODR, digit5CathodeGpioPortBit, 1);
		setBitValue(digit4CathodeGpioPort->ODR, digit4CathodeGpioPortBit, 1);
		setBitValue(digit3CathodeGpioPort->ODR, digit3CathodeGpioPortBit, 1);
	}

	void setDigit(FU8 digitIndex, FU8 digitBitsState) {
		this->turnOffDisplay();

//	setBitMaskedValues(PD_DDR, digit0CathodeD, bitsCountToMask(2), digitBitsState >> 0);
//	setBitValue(PC_DDR, digit2CathodeC, (digitBitsState >> 2) & 1);
//	setBitMaskedValues(PC_DDR, digit3CathodeC, bitsCountToMask(2), digitBitsState >> 3);
//	setBitMaskedValues(PA_DDR, digit5CathodeA, bitsCountToMask(3), digitBitsState >> 5);

//		digitBitsState = ~digitBitsState;
		#if 1
		setBitValue(digit0AnodeGpioPort->ODR, digit0AnodeGpioPortBit, hasBit(digitBitsState, 0));
		setBitValue(digit1AnodeGpioPort->ODR, digit1AnodeGpioPortBit, hasBit(digitBitsState, 1));
		setBitValue(digit2AnodeGpioPort->ODR, digit2AnodeGpioPortBit, hasBit(digitBitsState, 2));
		setBitValue(digit3AnodeGpioPort->ODR, digit3AnodeGpioPortBit, hasBit(digitBitsState, 3));
		setBitValue(digit4AnodeGpioPort->ODR, digit4AnodeGpioPortBit, hasBit(digitBitsState, 4));
		setBitValue(digit5AnodeGpioPort->ODR, digit5AnodeGpioPortBit, hasBit(digitBitsState, 5));
		// do not touch SWIM during debug
		#if NDEBUG
			setBitValue(digit6AnodeGpioPort->ODR, digit6AnodeGpioAndSwimPortBit, hasBit(digitBitsState, 6));
		#endif
		setBitValue(digitDotAnodeGpioPort->ODR, digitDotAnodeGpioPortBit, hasBit(digitBitsState, 7));

		#else
		setBitMaskedValues(digit0AnodeGpioPort->ODR, digit0AnodeGpioPortBit, bitsCountToMask(2), digitBitsState >> 0);
		setBitValue(digit2AnodeGpioPort->ODR, digit2AnodeGpioPortBit, (digitBitsState >> 2) & 1);
		setBitMaskedValues(digit4AnodeGpioPort->ODR, digit4AnodeGpioPortBit, bitsCountToMask(2), digitBitsState >> 3);
		setBitMaskedValues(digit3AnodeGpioPort->ODR, digit3AnodeGpioPortBit, bitsCountToMask(2), digitBitsState >> 5);
		#endif // 1

		switch(digitIndex) {
			case(0): setBitValue(digit0CathodeGpioPort->ODR, digit0CathodeGpioPortBit, 0); break;
			case(1): setBitValue(digit0CathodeGpioPort->ODR, digit1CathodeGpioPortBit, 0); break;
			case(2): setBitValue(digit2CathodeGpioPort->ODR, digit2CathodeGpioPortBit, 0); break;
			case(3): setBitValue(digit3CathodeGpioPort->ODR, digit3CathodeGpioPortBit, 0); break;
			case(4): setBitValue(digit4CathodeGpioPort->ODR, digit4CathodeGpioPortBit, 0); break;
			case(5): setBitValue(digit5CathodeGpioPort->ODR, digit5CathodeGpioPortBit, 0); break;
		}
	}

	void update() {
		this->setDigit(this->currentDisplayIndex, this->displayChars[this->currentDisplayIndex]);
		this->currentDisplayIndex += 1;
		if(this->currentDisplayIndex == 6) this->currentDisplayIndex = 0;
	}

};

Display display;

void displayOverflow(FU8* dest) {
	dest[0] = 0;
	dest[1] = _7SegmentsFont::d0;
	dest[2] = _7SegmentsFont::L;
}

void displayDecrimal(FU16 x, FU8* dest) {
	forDec(int, i,  0, 3) {
		dest[i] = _7SegmentsFont::digits[divmod10(&x)];
	}
}

void displayDecrimal6(FU16 x, FU8* dest) {
	forDec(int, i,  0, 6) {
		dest[i] = _7SegmentsFont::digits[divmod10(&x)];
	}
}

void display_fixLastDigit(FU16 x, FU8* dest, void (*display)(FU16 x, FU8* dest)) {
	//# small value
	if(1000 <= x) {
		display(x, dest);
	}
	else {
		FU8 newDigits[3];
		display(x, newDigits);
		//# prevent display last digit small changes (ADC error)
		if(newDigits[0] == dest[0] && newDigits[1] == dest[1]) {
			//# keep old digits
		}
		else {
			memcpy(dest, newDigits, 3);
		}
	}
}

typedef U16 U16_16SubShift_Shift;

struct Measurer {
	struct AdcUserFix {
		U16_16SubShift_Shift mul;
		I8 add;
		struct {
			U8 dummy: 3;
			U8 shift: 5;
		};
		Bool isValid() const {
			return mul != 0xFFFF && mul != 0;
		}
		FU16 fix(FU16 v) const {
			return (FU32(v + this->add) * this->mul) >> this->shift;
		}
	};

	struct Settings {
		U16 hysteresys;
		AdcUserFix adcFix;
	};

	virtual FU16 adcRead() = 0;
	virtual void displayDigit(FU16 x) = 0;
	virtual void displayOverflow() = 0;

	RunningAvg<FU16[adcMaxBufferSize], FU32> m_dataRunningAvg;
	FU16 m_lastDataAvgValue;
	FU16 m_value;

	Settings const* m_settingsPtr;

	Measurer() {
		m_lastDataAvgValue = FU16(-1) / 2;
	}

	void measureAdc(FU8 index) {
		m_value = (m_settingsPtr->adcFix.isValid()) ? ADCUser_transformOverflow(adcRead()) : -1;
		if(m_value != FU16(-1)) {
			m_dataRunningAvg.add(m_value);
		};
	}
	Bool isOverflow() const {
		return m_value == -1;
	}
	FU16 getValue() const {
		if(isOverflow()) {
			return -1;
		}
		else {
			return m_dataRunningAvg.computeAvg();
		}
	}

	void display() {
		if(m_value == FU16(-1)) {
			displayOverflow();
		}
		else {
			FU16 avg = m_dataRunningAvg.computeAvg();
			if(Math_abs(FI16(avg - m_lastDataAvgValue)) < m_settingsPtr->hysteresys * get10Power(avg, 100)) {
//				debug { displayDigit(666);  }
//				displayDigit(666);
			}
			else {
				m_lastDataAvgValue = avg;
				displayDigit(avg);
			}
		}
	}

};

typedef Measurer User_Measurer;

struct VoltageMeasurer: User_Measurer {
	virtual FU16 adcRead() override {
		return ADC_readSync(voltageAdcChannelNo);
	}

	enum { display_displayChars_offset = 0 };
	//# 0 < x <= 999(9.99V) => x.xx V
	//# 1000(10.0V) <= x <= 9999(99.99V) => xx.x / 10 V
	//# 10000(100V) <= x <= 65534(655.34V) => xxx / 100 V
	//# 65535 == x => OL (handled externally)
	virtual void displayDigit(FU16 x) {
		FU8* dest = &(::display.displayChars[display_displayChars_offset]);
		const FU16 xVal = x;
		forInc(int, i,  0, 2) (1000 <= x) && (divmod10(&x));

		forDec(int, i,  0, 3) {
			dest[i] = _7SegmentsFont::digits[divmod10(&x)];
		}

		if(10000 <= xVal) {

		}
		else if(1000 <= xVal) {
			dest[1] |= _7SegmentsFont::dot;
		}
		else {
			dest[0] |= _7SegmentsFont::dot;
		}
	}

	virtual void displayOverflow() {
		::displayOverflow(&(::display.displayChars[display_displayChars_offset]));
	};

} voltageMeasurer /* = {
	.m_settingsPtr = &(settings.voltageMeasurerSettings),
	.m_lastDataAvgValue = 0,
} */;

struct CurrentMeasurer: User_Measurer {
	virtual FU16 adcRead() override {
		return ADC_readSync(currentAdcChannelNo);
	}

	enum { display_displayChars_offset = 3 };
	//# 0 < x <= 999(999mA) => xxx mA
	//# 999(999mA) < x <= 9999(9.999A) => x.xx / 10 A
	//# 9999(9.999A) < x <= 65534(65.534A) => xx.x / 100 A
	//# 65535 == x => OL (handled externally)
	void displayDigit(FU16 x) {
		FU8* dest = &(::display.displayChars[display_displayChars_offset]);
		const FU16 xVal = x;
		forInc(FU8, i, 0, 2) (1000 <= xVal) && (divmod10(&x));

		forDec(int, i,  0, 3) {
			dest[i] = _7SegmentsFont::digits[divmod10(&x)];
		}

		if(10000 <= xVal) {
			dest[1] |= _7SegmentsFont::dot;
		}
		else if(1000 <= xVal) {
			dest[0] |= _7SegmentsFont::dot;
		}
		else {
		}
	}

	virtual void displayOverflow() {
		::displayOverflow(&(::display.displayChars[display_displayChars_offset]));
	};

} currentMeasurer /* = {
	.m_settingsPtr = &(settings.voltageMeasurerSettings),
	.m_lastDataAvgValue = 0,
} */;


struct Settings {
	User_Measurer::Settings voltageMeasurerSettings;
	User_Measurer::Settings currentMeasurerSettings;

	U16 displayUpdatePeriod;
};

EEMEM const Settings defaultSettings = {
	.voltageMeasurerSettings = {
		.adcFix = { .mul = U16_16SubShift_Shift(1550), .add = 0, .shift = 10 },
		.hysteresys = 6,
	},
	.currentMeasurerSettings = {
		.adcFix = { .mul = U16_16SubShift_Shift(500), .add = -5, .shift = 10 },
		.hysteresys = 5,
	},
	.displayUpdatePeriod = ticksCountPerSReal  / 3,
};
Settings const& settings = ((Settings*)(&defaultSettings))[0];

BGA__RUN {
	voltageMeasurer.m_settingsPtr = &(settings.voltageMeasurerSettings);
	currentMeasurer.m_settingsPtr = &(settings.currentMeasurerSettings);
}

FU16 ticksCount = 0;
FU16 displayTicksCount = 0;
FU16 adcFetchIndex = 0;

ISR(TIM4_ISR) {
	clearBit(TIM4_SR, TIM4_SR_UIF);

	ticksCount += 1;

	display.update();

	#if 1
	if((ticksCount & bitsCountToMask(adcFetchSpeedPrescaler - 1))) {
	}
	else {
		if((ticksCount & bitsCountToMask(adcFetchSpeedPrescaler))) {
			voltageMeasurer.measureAdc(0);
		}
		else {
			currentMeasurer.measureAdc(0);
		}
	}
	#endif

	#if 1
	if(settings.displayUpdatePeriod <= (displayTicksCount += 2) ) {
		displayTicksCount = 0;
		adcFetchIndex += 1;
		if(adcFetchIndex & 1) {
			voltageMeasurer.display();
		}
		else {
			currentMeasurer.display();
		}
	}
	#endif
}

void Clock_setCpuFullSpeed() {
	CLK_CKDIVR = 0;
}

void Hw_enable() {
	enum {
		CLK_PCKENR1_TIM4 = 4
	};
	CLK_PCKENR1 = _BV(CLK_PCKENR1_TIM4);

	enum {
		CLK_PCKENR12_ADC = 3
	};
	CLK_PCKENR2 = _BV(CLK_PCKENR12_ADC);
}

void main() {

	Clock_setCpuFullSpeed();
	Hw_enable();
	display.init();

	forInc(FU8, i, 0, 6) {
		display.displayChars[i] = _7SegmentsFont::digits[i];
	}

	ADC_init();
	ADC_initChannel(currentAdcChannelNo);
	ADC_initChannel(voltageAdcChannelNo);

	Timer_init();
	enable_interrupts();

	while(1) __wait_for_interrupt();
}
