/*
 * Copyright (c) 2022 University of Padova, Dep. of Information Engineering, SIGNET lab.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mattia Sandri
 */

#include "circular-aperture-antenna-model.h"

#include "antenna-model.h"

#include <ns3/double.h>
#include <ns3/log.h>

#include <math.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CircularApertureAntennaModel");

NS_OBJECT_ENSURE_REGISTERED(CircularApertureAntennaModel);

TypeId
CircularApertureAntennaModel::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::CircularApertureAntennaModel")
            .SetParent<AntennaModel>()
            .SetGroupName("Antenna")
            .AddConstructor<CircularApertureAntennaModel>()
            .AddAttribute("AntennaMaxGainDb",
                          "The maximum gain value in dB of the antenna",
                          DoubleValue(1),
                          MakeDoubleAccessor(&CircularApertureAntennaModel::SetMaxGain),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("AntennaCircularApertureRadius",
                          "The radius of the aperture of the antenna, in meters",
                          DoubleValue(0.5),
                          MakeDoubleAccessor(&CircularApertureAntennaModel::SetApertureRadius),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("OperatingFrequency",
                          "The operating frequency of the antenna",
                          DoubleValue(2e9),
                          MakeDoubleAccessor(&CircularApertureAntennaModel::SetOperatingFrequency),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("AntennaInclination",
                          "The inclination angle in rad of the antenna",
                          DoubleValue(0.0),
                          MakeDoubleAccessor(&CircularApertureAntennaModel::SetInclination),
                          MakeDoubleChecker<double>(0.0, M_PI))
            .AddAttribute("AntennaAzimuth",
                          "The azimuth angle in rad of the antenna",
                          DoubleValue(0.0),
                          MakeDoubleAccessor(&CircularApertureAntennaModel::SetAzimuth),
                          MakeDoubleChecker<double>(-M_PI, M_PI));
    return tid;
}

CircularApertureAntennaModel::CircularApertureAntennaModel()
    : AntennaModel()
{
}

CircularApertureAntennaModel::~CircularApertureAntennaModel()
{
}

void
CircularApertureAntennaModel::SetOrientation(Angles a)
{
    NS_LOG_FUNCTION(this << a);
    m_antennaOrientation = a;
}

void
CircularApertureAntennaModel::SetInclination(double theta)
{
    NS_LOG_FUNCTION(this << theta);
    NS_ASSERT_MSG((0 <= theta && theta <= M_PI),
                  "Setting invalid inclination(deg): " << RadiansToDegrees(theta));
    m_antennaOrientation = Angles(m_antennaOrientation.GetAzimuth(), theta);
}

double
CircularApertureAntennaModel::GetInclination()
{
    return m_antennaOrientation.GetInclination();
}

void
CircularApertureAntennaModel::SetAzimuth(double phi)
{
    NS_LOG_FUNCTION(this << phi);
    NS_ASSERT_MSG((-M_PI <= phi && phi <= M_PI),
                  "Setting invalid azimuth(deg): " << RadiansToDegrees(phi));
    m_antennaOrientation = Angles(phi, m_antennaOrientation.GetInclination());
}

double
CircularApertureAntennaModel::GetAzimuth()
{
    return m_antennaOrientation.GetAzimuth();
}

void
CircularApertureAntennaModel::SetApertureRadius(double r)
{
    NS_LOG_FUNCTION(this << r);
    m_apertureRadius = r;
}

double
CircularApertureAntennaModel::GetApertureRadius()
{
    return m_apertureRadius;
}

void
CircularApertureAntennaModel::SetOperatingFrequency(double f)
{
    NS_LOG_FUNCTION(this << f);
    m_operatingFrequency = f;
}

double
CircularApertureAntennaModel::GetOperatingFrequency()
{
    return m_operatingFrequency;
}

void
CircularApertureAntennaModel::SetMaxGain(double gain)
{
    NS_LOG_FUNCTION(this << gain);
    m_maxGain = gain;
}

double
CircularApertureAntennaModel::GetMaxGain()
{
    return m_maxGain;
}

double
CircularApertureAntennaModel::GetGainDb(Angles a)
{
    NS_LOG_FUNCTION(this << a);

    double theta1 = m_antennaOrientation.GetInclination();
    double phi1 = m_antennaOrientation.GetAzimuth();
    double theta2 = a.GetInclination();
    double phi2 = a.GetAzimuth();

    // For this class the azimuth angle phi is [-pi,pi], but the ISO convention suppose
    // phi in [0,2*pi], so a conversion is needed
    if (phi1 > -M_PI && phi1 < 0)
    {
        phi1 = 2 * M_PI - abs(phi1);
    }
    if (phi2 > -M_PI && phi2 < 0)
    {
        phi2 = 2 * M_PI - abs(phi2);
    }

    // Convert the spherical coordinates to Cartesian coordinates
    double x1 = sin(theta1) * cos(phi1);
    double y1 = sin(theta1) * sin(phi1);
    double z1 = cos(theta1);

    double x2 = sin(theta2) * cos(phi2);
    double y2 = sin(theta2) * sin(phi2);
    double z2 = cos(theta2);

    // Calculate the angle between the incoming ray and the antenna bore sight
    double theta =
        acos((x1 * x2 + y1 * y2 + z1 * z2) / ((sqrt(pow(x1, 2) + pow(y1, 2) + pow(z1, 2))) *
                                              (sqrt(pow(x2, 2) + pow(y2, 2) + pow(z2, 2)))));

    double gain = 0;

    if (theta == 0)
    {
        gain = m_maxGain;
    }
    else if (theta < -M_PI_2 || theta > M_PI_2)
    {
        gain = m_maxGain - 100; // This is an approximation. 3GPP TR38.811 does not give indications
                                // on how the antenna field pattern is over it's 180 degrees FOV
    }
    else // theta =! 0 and -90deg<theta<90deg
    {
        double k = (2 * M_PI * m_operatingFrequency) / 299792458;
        double J_1 = std::cyl_bessel_j(1, (k * m_apertureRadius * sin(theta)));
        double denominator = k * m_apertureRadius * sin(theta);
        gain = 4 * pow(abs((J_1 / denominator)), 2);
        gain = 10 * log10(gain) + m_maxGain;
    }

    return gain;
}

} // namespace ns3
