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

#ifndef CIRCULAR_APERTURE_ANTENNA_MODEL_H
#define CIRCULAR_APERTURE_ANTENNA_MODEL_H

#include <ns3/antenna-model.h>
#include <ns3/object.h>

namespace ns3
{

/**
 * \ingroup antenna
 *
 * \brief Circular Aperture Antenna Model, as described in 3GPP 38.811 6.4.1
 */
class CircularApertureAntennaModel : public AntennaModel
{
  public:
    CircularApertureAntennaModel(void);

    virtual ~CircularApertureAntennaModel(void);

    /**
     * \brief Get the type ID.
     * \return The object TypeId.
     */
    static TypeId GetTypeId();

    /**
     * \brief Set the antenna orientation using azimuth-inclination convention
     *
     * \param a the orientation angles of the antenna
     */
    void SetOrientation(Angles a);

    /**
     * \brief Set the antenna inclination using azimuth-inclination convention
     *
     * \param theta the inclination angle of the antenna in rad
     */
    void SetInclination(double theta);

    /**
     * \brief Return the antenna inclination using azimuth-inclination convention
     *
     * \return the inclination angle of the antenna in rad
     */
    double GetInclination();

    /**
     * \brief Set the antenna azimtuh using azimuth-inclination convention
     *
     * \param theta the azimuth angle of the antenna in rad
     */
    void SetAzimuth(double theta);

    /**
     * \brief Return the antenna azimuth using azimuth-inclination convention
     *
     * \return the azimuth angle of the antenna in rad
     */
    double GetAzimuth();

    /**
     * \brief Set the antenna aperture radius
     *
     * \param r the antenna radius in meters
     */
    void SetApertureRadius(double r);

    /**
     * \brief Return the antenna aperture radius
     *
     * \return the antenna radius in meters
     */
    double GetApertureRadius();

    /**
     * \brief Set the antenna operating frequency
     *
     * \param f the antenna operating freqyency, in Hz
     */
    void SetOperatingFrequency(double f);

    /**
     * \brief Return the antenna operating frequency
     *
     * \return the antenna operating freqyency, in Hz
     */
    double GetOperatingFrequency();

    /**
     * \brief Set the antenna max gain
     *
     * \param gain the antenna max gain in dB
     */
    void SetMaxGain(double gain);

    /**
     * \brief Return the antenna max gain
     *
     * \return the antenna max gain in dB
     */
    double GetMaxGain();

    /**
     * \brief Get the gain in dB, using Bessel equation of first kind and first order.
     *
     * \param a the position at which the gain need to be calculated with respect to the antenna
     * position
     *
     * \return the antenna gain at the specified Angles a
     */
    virtual double GetGainDb(Angles a) override;

  private:
    Angles m_antennaOrientation{
        0.0,
        0.0};                    //!< antenna orientation using the azimuth-inclination convention.
    double m_apertureRadius;     //!< antenna aperture radius
    double m_operatingFrequency; //!< antenna operating frequency
    double m_maxGain;            //!< antenna gain in dB towards the main orientation
};

} // namespace ns3

#endif // CIRCULAR_APERTURE_ANTENNA_MODEL_H
