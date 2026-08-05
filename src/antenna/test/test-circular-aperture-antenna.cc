/*
 *   Copyright (c) 2023 University of Padova, Dep. of Information Engineering, SIGNET lab.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation;
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "cmath"
#include "iostream"
#include "sstream"
#include "string"

#include "ns3/circular-aperture-antenna-model.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TestCircularApertureAntennaModel");

/**
 * \ingroup antenna-tests
 *
 * \brief CircularApertureAntennaModel Test Case
 */
class CircularApertureAntennaModelTestCase : public TestCase
{
  public:
    /**
     * Generate a string containing all relevant parameters
     * \param AntennaMaxGainDb the antenna maximum possible gain [dB]
     * \param AntennaCircularApertureRadius the radius of the parabolic aperture [m]
     * \param OperatingFrequency operating frequency [Hz]
     * \param AntennaInclination antenna inclination [rad]
     * \param AntennaAzimuth antenna azimuth [rad]
     * \return the string containing all relevant parameters
     */
    static std::string BuildNameString(double AntennaMaxGainDb,
                                       double AntennaCircularApertureRadius,
                                       double OperatingFrequency,
                                       double AntennaInclination,
                                       double AntennaAzimuth);
    /**
     * The constructor of the test case
     * \param AntennaMaxGainDb the antenna maximum possible gain [dB]
     * \param AntennaCircularApertureRadius the radius of the parabolic aperture [m]
     * \param OperatingFrequency operating frequency [Hz]
     * \param AntennaInclination antenna inclination [rad]
     * \param AntennaAzimuth antenna azimuth [rad]
     * \param TestInclination the inclination to perform the gain test [rad]
     * \param TestAzimuth the azimuth to perform the gain test [rad]
     * \param expectedGainDb the expected antenna gain [dB]
     */
    CircularApertureAntennaModelTestCase(double AntennaMaxGainDb,
                                         double AntennaCircularApertureRadius,
                                         double OperatingFrequency,
                                         double AntennaInclination,
                                         double AntennaAzimuth,
                                         double TestInclination,
                                         double TestAzimuth,
                                         double expectedGainDb);

  private:
    /**
     * Run the test
     */
    void DoRun() override;
    /**
     * Compute the gain of the antenna
     * \param a the antenna
     * \param Inclination antenna inclination [rad]
     * \param Azimuth antenna azimuth [rad]
     * \return the gain of the antenna [dB]
     */
    double ComputeGain(Ptr<CircularApertureAntennaModel> a, double Inclination, double Azimuth);

    double m_antennaMaxGainDb;              //!< the antenna maximum possible gain [dB]
    double m_antennaCircularApertureRadius; //!< the radius of the parabolic aperture [m]
    double m_operatingFrequency;            //!< operating frequency [Hz]
    double m_antennaInclination;            //!< antenna inclination [rad]
    double m_antennaAzimuth;                //!< antenna azimuth [rad]
    double m_testInclination;               //!< test antenna inclination [rad]
    double m_testAzimuth;                   //!< test antenna azimuth [rad]
    double m_expectedGain;                  //!< the expected antenna gain [dB]
};

std::string
CircularApertureAntennaModelTestCase::BuildNameString(double AntennaMaxGainDb,
                                                      double AntennaCircularApertureRadius,
                                                      double OperatingFrequency,
                                                      double AntennaInclination,
                                                      double AntennaAzimuth)
{
    std::ostringstream oss;
    oss << "Gain=" << AntennaMaxGainDb << "dB"
        << ", radius=" << AntennaCircularApertureRadius << "m"
        << ", frequency" << OperatingFrequency << "Hz"
        << ", inclination=" << RadiansToDegrees(AntennaInclination) << " deg"
        << ", azimuth=" << RadiansToDegrees(AntennaAzimuth) << " deg";
    return oss.str();
}

CircularApertureAntennaModelTestCase::CircularApertureAntennaModelTestCase(
    double AntennaMaxGainDb,
    double AntennaCircularApertureRadius,
    double OperatingFrequency,
    double AntennaInclination,
    double AntennaAzimuth,
    double TestInclination,
    double TestAzimuth,
    double expectedGainDb)
    : TestCase(BuildNameString(AntennaMaxGainDb,
                               AntennaCircularApertureRadius,
                               OperatingFrequency,
                               AntennaInclination,
                               AntennaAzimuth)),
      m_antennaMaxGainDb(AntennaMaxGainDb),
      m_antennaCircularApertureRadius(AntennaCircularApertureRadius),
      m_operatingFrequency(OperatingFrequency),
      m_antennaInclination(AntennaInclination),
      m_antennaAzimuth(AntennaAzimuth),
      m_expectedGain(expectedGainDb)
{
}

double
CircularApertureAntennaModelTestCase::ComputeGain(Ptr<CircularApertureAntennaModel> a,
                                                  double Inclination,
                                                  double Azimuth)
{
    double theta1 = a->GetInclination();
    double phi1 = a->GetAzimuth();
    double theta2 = Inclination;
    double phi2 = Azimuth;

    double maxGain = a->GetMaxGain();
    double apertureRadius = a->GetApertureRadius();
    double operatingFrequency = a->GetOperatingFrequency();

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
        gain = maxGain;
    }
    else if (theta < -M_PI_2 || theta > M_PI_2)
    {
        gain = maxGain - 100; // This is an approximation. 3GPP TR38.811 does not give indications
                              // on how the antenna field pattern is over it's 180 degrees FOV
    }
    else // theta =! 0 and -90deg<theta<90deg
    {
        double k = (2 * M_PI * operatingFrequency) / 299792458;
        double J_1 = std::cyl_bessel_j(1, (k * apertureRadius * sin(theta)));
        double denominator = k * apertureRadius * sin(theta);
        gain = 4 * pow(abs((J_1 / denominator)), 2);
        gain = 10 * log10(gain) + maxGain;
    }

    return gain;
}

void
CircularApertureAntennaModelTestCase::DoRun()
{
    NS_LOG_FUNCTION(this << BuildNameString(m_antennaMaxGainDb,
                                            m_antennaCircularApertureRadius,
                                            m_operatingFrequency,
                                            m_antennaInclination,
                                            m_antennaAzimuth));

    Ptr<CircularApertureAntennaModel> a = CreateObject<CircularApertureAntennaModel>();
    a->SetAttribute("AntennaMaxGainDb", DoubleValue(m_antennaMaxGainDb));
    a->SetAttribute("AntennaCircularApertureRadius", DoubleValue(m_antennaCircularApertureRadius));
    a->SetAttribute("OperatingFrequency", DoubleValue(m_operatingFrequency));
    a->SetAttribute("AntennaInclination", DoubleValue(m_antennaInclination));
    a->SetAttribute("AntennaAzimuth", DoubleValue(m_antennaAzimuth));

    double actualGainDb = ComputeGain(a, m_testInclination, m_testAzimuth);
    NS_TEST_EXPECT_MSG_EQ_TOL(actualGainDb,
                              m_expectedGain,
                              0.001,
                              "wrong value of the radiation pattern");
}

/**
 * \ingroup antenna-tests
 *
 * \brief UniformPlanarArray Test Suite
 */
class CircularApertureAntennaModelTestSuite : public TestSuite
{
  public:
    CircularApertureAntennaModelTestSuite();
};

CircularApertureAntennaModelTestSuite::CircularApertureAntennaModelTestSuite()
    : TestSuite("circular-aperture-antenna-test", UNIT)
{
    AddTestCase(new CircularApertureAntennaModelTestCase(30,
                                                         0.5,
                                                         2e9,
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         30),
                TestCase::QUICK);
    AddTestCase(new CircularApertureAntennaModelTestCase(30,
                                                         2,
                                                         20e9,
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         30),
                TestCase::QUICK);
    AddTestCase(new CircularApertureAntennaModelTestCase(30,
                                                         0.5,
                                                         2e9,
                                                         DegreesToRadians(10),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         2.753840),
                TestCase::QUICK);
    AddTestCase(new CircularApertureAntennaModelTestCase(30,
                                                         2,
                                                         20e9,
                                                         DegreesToRadians(10),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         -42.0104),
                TestCase::QUICK);
    AddTestCase(new CircularApertureAntennaModelTestCase(30,
                                                         0.5,
                                                         2e9,
                                                         DegreesToRadians(180),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         -70),
                TestCase::QUICK);
    AddTestCase(new CircularApertureAntennaModelTestCase(30,
                                                         2,
                                                         20e9,
                                                         DegreesToRadians(180),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         DegreesToRadians(0),
                                                         -70),
                TestCase::QUICK);
}

static CircularApertureAntennaModelTestSuite staticCircularApertureAntennaModelTestSuiteInstance;
