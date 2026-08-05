/*
 * Copyright (c) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef SGP4_CSV_MOBILITY_MODEL_H
#define SGP4_CSV_MOBILITY_MODEL_H

#include "geocentric-constant-position-mobility-model.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace ns3
{

/**
 * \ingroup mobility
 *
 * \brief Mobility model that reads SGP4-computed satellite trajectories from CSV files
 *
 * This model extends GeocentricConstantPositionMobilityModel to support time-varying
 * satellite positions computed by SGP4. It reads a CSV file containing satellite
 * positions over time and interpolates between them.
 *
 * Expected CSV format:
 * time,satellite,elevation_deg,azimuth_deg,distance_km,lat_subpoint,lon_subpoint,height_km,x_ecef_km,y_ecef_km,z_ecef_km
 *
 * The model can be used in two modes:
 * 1. Single satellite: Specify satellite name, model updates position over simulation time
 * 2. Multiple satellites: Create one model instance per satellite
 */
class Sgp4CsvMobilityModel : public GeocentricConstantPositionMobilityModel
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    /**
     * \brief Constructor
     */
    Sgp4CsvMobilityModel();

    /**
     * \brief Destructor
     */
    virtual ~Sgp4CsvMobilityModel();

    /**
     * \brief Load satellite trajectory data from CSV file
     * \param filename Path to CSV file with SGP4 data
     * \param satelliteName Name of the satellite to track (e.g., "STARLINK-1199")
     * \return true if successful, false otherwise
     */
    bool LoadTrajectory(const std::string& filename, const std::string& satelliteName);

    /**
     * \brief Get the current position (updates based on simulation time)
     * \return Current position vector
     */
    void UpdatePosition();

    /**
     * \brief Get the velocity at current time
     * \return Velocity vector in m/s
     */
    virtual Vector DoGetVelocity(void) const override;

    /**
     * \brief Set whether to use ECEF coordinates or geographic coordinates from CSV
     * \param useEcef true to use ECEF (x,y,z), false to use (lat,lon,alt)
     */
    void SetUseEcefCoordinates(bool useEcef);

  private:
    /**
     * \brief Structure to hold satellite position data at a specific time
     */
    struct PositionData
    {
        double time;         //!< Time in seconds from start
        double lat;          //!< Latitude in degrees
        double lon;          //!< Longitude in degrees
        double alt;          //!< Altitude in meters
        double x_ecef;       //!< ECEF X coordinate in meters
        double y_ecef;       //!< ECEF Y coordinate in meters
        double z_ecef;       //!< ECEF Z coordinate in meters
        double elevation;    //!< Elevation angle in degrees
        double azimuth;      //!< Azimuth angle in degrees
        double distance;     //!< Distance in meters
    };

    /**
     * \brief Parse timestamp string to seconds
     * \param timestamp Timestamp string (e.g., "2025-11-26 14:16:50")
     * \return Time in seconds from reference time (first entry)
     */
    double ParseTimestamp(const std::string& timestamp) const;

    /**
     * \brief Interpolate position between two data points
     * \param t Current simulation time
     * \return Interpolated position
     */
    Vector InterpolatePosition(double t) const;

    /**
     * \brief Calculate velocity from position data
     * \param t Current simulation time
     * \return Velocity vector
     */
    Vector CalculateVelocity(double t) const;

    std::vector<PositionData> m_trajectory; //!< Satellite trajectory data
    std::string m_satelliteName;            //!< Name of satellite being tracked
    bool m_useEcef;                         //!< Use ECEF coordinates if true, geographic if false
    double m_startTime;                     //!< Simulation start time reference
    bool m_trajectoryLoaded;                //!< Flag indicating if trajectory is loaded
};

} // namespace ns3

#endif /* SGP4_CSV_MOBILITY_MODEL_H */
