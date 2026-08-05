/*
 * Copyright (c) 2006,2007 INRIA
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
#ifndef GEOCENTRIC_CONSTANT_POSITION_MOBILITY_MODEL_H
#define GEOCENTRIC_CONSTANT_POSITION_MOBILITY_MODEL_H

#include "geographic-positions.h"
#include "mobility-model.h"

namespace ns3
{

/**
 * \ingroup mobility
 *
 * \brief Mobility model using geocentric euclidean coordinates, as defined in 38.811 chapter 6.3
 */
class GeocentricConstantPositionMobilityModel : public MobilityModel
{
  public:
    /**
     * Register this type with the TypeId system.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);
    /**
     * Create a position located at coordinates (0,0,0)
     */
    GeocentricConstantPositionMobilityModel();
    virtual ~GeocentricConstantPositionMobilityModel();

    /**
     * \brief Computes elevation angle between a ground terminal and a HAPS/Satellite.
     * After calculating the plane perpendicular to one cartesian position vector,
     * the elevation angle is calculated using
     * https://www.w3schools.blog/angle-between-a-line-and-a-plane. The altitude of the node passed
     * as parameter needs to be higher.
     * \param other pointer to the HAPS/Satellite mobility model
     * \return the elevation angle in degrees
     */
    virtual double GetElevationAngle(Ptr<const GeocentricConstantPositionMobilityModel> other);

    /**
     * \brief Get the position using geographic (geodetic) coordinates
     * \return Vector containing (latitude, longitude, altitude)
     */
    virtual Vector GetGeographicPosition(void) const;

    /**
     * \brief Set the position using geographic coordinates
     * \param position pointer to a Vector containing (latitude, longitude, altitude)
     */
    virtual void SetGeographicPosition(const Vector& position);

    /**
     * \brief Get the position using Geocentric Cartesian coordinates
     * \return Vector containing (X, Y, Z)
     */
    virtual Vector GetGeocentricPosition(void) const;

    /**
     * \brief Set the position using geographic coordinates
     * \param position pointer to a Vector containing (latitude, longitude, altitude)
     */
    virtual void SetGeocentricPosition(const Vector& position);

    /**
     * \brief Set the reference point for coordinate translation
     * \param refPoint vector containing the geographic reference point
     */
    virtual void SetCoordinateTranslationReferencePoint(const Vector& refPoint);

    /**
     * \brief Get the reference point for coordinate translation
     * \return Vector containing geographic reference point
     */
    virtual Vector GetCoordinateTranslationReferencePoint(void) const;

    /**
     * \brief Get the position in planar Cartesian coordinates, using m_geographicReferencePoint as
     * reference. \return Vector containing the planar Cartesian coordinates (X, Y, Z)
     */
    virtual Vector GetPosition(void) const;

    /**
     * \brief Set the position in planar Cartesian coordinates, using m_geographicReferencePoint as
     * reference. \param position Vector containing the planar Cartesian coordinates (X, Y, Z)
     */
    virtual void SetPosition(const Vector& position);

    /**
     * \brief Get the distance between two nodes.
     * \param other the mobility model for which the distance from will be calculated
     * \return the distance in meters
     */
    double GetDistanceFrom(Ptr<const GeocentricConstantPositionMobilityModel> other) const;

  private:
    /**
     * \brief Get the position in planar Cartesian coordinates, using m_geographicReferencePoint as
     * reference. \return Vector containing the planar Cartesian coordinates (X, Y, Z)
     */
    virtual Vector DoGetPosition(void) const;
    /**
     * \brief Set the position in planar Cartesian coordinates, using m_geographicReferencePoint as
     * reference. \param position Vector containing the planar Cartesian coordinates (X, Y, Z)
     */
    virtual void DoSetPosition(const Vector& position);
    /**
     * \brief Get the distance between two nodes.
     * \param other the mobility model for which the distance from will be calculated
     * \return the distance in meters
     */
    double DoGetDistanceFrom(Ptr<const GeocentricConstantPositionMobilityModel> other) const;
    /**
     * \brief Get the position using geographic (geodetic) coordinates
     * \return Vector containing (latitude, longitude, altitude)
     */
    virtual Vector DoGetGeographicPosition(void) const;
    /**
     * \brief Set the position using geographic coordinates
     * \param position pointer to a Vector containing (latitude, longitude, altitude)
     */
    virtual void DoSetGeographicPosition(const Vector& position);
    /**
     * \brief Get the position using Geocentric Cartesian coordinates
     * \return Vector containing (X, Y, Z)
     */
    virtual Vector DoGetGeocentricPosition(void) const;
    /**
     * \brief Set the position using Geocentric coordinates
     * \param position pointer to a Vector containing (X, Y, Z)
     */
    virtual void DoSetGeocentricPosition(const Vector& position);
    /**
     * \brief Computes elevation angle between a ground terminal and a HAPS/Satellite.
     * After calculating the plane perpendicular to one cartesian position vector,
     * the elevation angle is calculated using
     * https://www.w3schools.blog/angle-between-a-line-and-a-plane. The altitude of the node passed
     * as parameter needs to be higher. \param other pointer to the HAPS/Satellite mobility model
     * \return the elevation angle in degrees
     */
    virtual double DoGetElevationAngle(Ptr<const GeocentricConstantPositionMobilityModel> other);
    /**
     * \brief Set the reference point for coordinate translation
     * \param refPoint vector containing the geographic reference point
     */
    virtual void DoSetCoordinateTranslationReferencePoint(const Vector& refPoint);
    /**
     * \brief Get the reference point for coordinate translation
     * \return Vector containing geographic reference point
     */
    virtual Vector DoGetCoordinateTranslationReferencePoint(void) const;

    virtual Vector DoGetVelocity(void) const;

    /**
     * the constant Geographic position, in degrees, in the order:
     * latitude
     * longitude
     * altitude
     */
    Vector m_position;

    /**
     * This is the point taken as reference when converting
     * from geographic to topographic (aka planar Cartesian)
     */
    Vector m_geographicReferencePoint{0, 0, 0};
};

} // namespace ns3

#endif /* GEOCENTRIC_CONSTANT_POSITION_MOBILITY_MODEL_H */
