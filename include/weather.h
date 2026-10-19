#pragma once
#include "render.h"

// Weather for the side column and the HOME-key hourly screen, fetched by the
// device from Open-Meteo (open-meteo.com) for TRANSIT_LAT/TRANSIT_LON. With
// no location configured, nothing is fetched and both stay invalid.

// Call every loop cycle: fetches every 15 min (2 min after a failure).
void weatherPoll();
// Latest forecast; `valid` is false until the first successful fetch, and
// the panel then just leaves the weather out.
WeatherInfo weatherGet();
HourlyInfo hourlyGet();
