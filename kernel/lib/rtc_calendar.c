/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rtc_calendar.c
 * Description: Pure calendar helpers for RTC → Unix time (no I/O).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/rtc.h>

uint8_t rtc_bcd_to_binary(uint8_t bcd)
{
	return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

uint16_t rtc_civil_year(const rtc_time_t *rt)
{
	uint16_t year;

	if (!rt)
		return 1970;

	if (rt->century > 0 && rt->century < 100)
		year = (uint16_t)(rt->century * 100 + (rt->year % 100));
	else if (rt->year >= 1970)
		year = rt->year;
	else
		year = (uint16_t)(2000 + (rt->year % 100));

	if (year < 1970)
		year = 1970;
	return year;
}

/*
 * Convert civil date/time (UTC) to seconds since 1970-01-01.
 * Leap years: Gregorian. No leap seconds.
 */
time_t rtc_fields_to_unix(const rtc_time_t *rt)
{
	uint16_t year;
	int leap;
	time_t days = 0;
	static const int days_in_month[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};

	if (!rt)
		return 0;

	year = rtc_civil_year(rt);

	leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;

	for (uint16_t y = 1970; y < year; y++)
		days += 365 + ((y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0);

	for (int m = 1; m < (int)rt->month && m <= 12; m++)
		days += days_in_month[m - 1] + (m == 2 ? leap : 0);

	if (rt->day > 0 && rt->day <= 31)
		days += (rt->day - 1);

	return (time_t)days * 86400 +
	       (rt->hour < 24 ? rt->hour : 0) * 3600 +
	       (rt->minute < 60 ? rt->minute : 0) * 60 +
	       (rt->second < 60 ? rt->second : 0);
}

void rtc_apply_cmos_format(rtc_time_t *time, uint8_t status_b)
{
	uint8_t hour_raw;

	if (!time)
		return;

	hour_raw = time->hour;

	if (!(status_b & RTC_STATUS_B_BINARY))
	{
		time->second = rtc_bcd_to_binary(time->second);
		time->minute = rtc_bcd_to_binary(time->minute);
		time->day = rtc_bcd_to_binary(time->day);
		time->month = rtc_bcd_to_binary(time->month);
		time->year = rtc_bcd_to_binary((uint8_t)(time->year & 0xFF));
		time->century = rtc_bcd_to_binary(time->century);
		/* Hour: strip PM bit before BCD decode when in 12h mode. */
		if (!(status_b & RTC_STATUS_B_24HOUR))
			time->hour = rtc_bcd_to_binary((uint8_t)(hour_raw & 0x7F));
		else
			time->hour = rtc_bcd_to_binary(hour_raw);
	}
	else if (!(status_b & RTC_STATUS_B_24HOUR))
	{
		time->hour = (uint8_t)(hour_raw & 0x7F);
	}

	if (!(status_b & RTC_STATUS_B_24HOUR))
	{
		/* CMOS: bit 7 of raw hour = PM in 12-hour mode. */
		if (hour_raw & 0x80)
		{
			if (time->hour < 12)
				time->hour = (uint8_t)(time->hour + 12);
		}
		else if (time->hour == 12)
		{
			time->hour = 0;
		}
	}
}
