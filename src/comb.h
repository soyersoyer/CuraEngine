/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#ifndef COMB_H
#define COMB_H

#include "utils/polygon.h"

namespace cura 
{
    
struct CombPath : public  std::vector<Point> //!< A single path either inside or outise the parts
{
    bool throughAir = false; //!< Whether the path is one which moves through air.
    bool cross_boundary = false; //!< Whether the path crosses a boundary.
};
struct CombPaths : public  std::vector<CombPath> //!< A list of paths alternating between inside a part and outside a part
{
}; 

/*!
 * Class for generating a combing move action from point a to point b and avoiding collision with other parts when moving through air.
 * See LinePolygonsCrossings::comb.
 * 
 * The general implementation is by rotating everything such that the the line segment from a to b is aligned with the x-axis.
 * We call the line on which a and b lie the 'scanline'.
 * 
 * The basic path is generated by following the scanline until it hits a polygon, then follow the polygon until the last point where it hits the scanline, 
 * follow the scanline again, etc.
 * The path is offsetted from the polygons, so that it doesn't intersect with them.
 * 
 * Next the basic path is optimized by taking shortcuts where possible. Only shortcuts which skip a single point are considered, in order to reduce computational complexity.
 */
class LinePolygonsCrossings
{
private:
    
    /*!
     * A Crossing holds data on a single point where a polygon crosses the scanline.
     */
    struct Crossing
    {
        int64_t x; //!< x coordinate of crossings between the polygon and the scanline.
        unsigned int point_idx; //!< The index of the first point of the line segment which crosses the scanline
        
        /*!
         * Creates a Crossing with minimal initialization
         * \param x The x-coordinate in transformed space
         * \param point_idx The index of the first point of the line segment which crosses the scanline
         */
        Crossing(int64_t x, unsigned int point_idx)
        : x(x), point_idx(point_idx)
        {
        }
    };
    
    /*!
     * A PolyCrossings holds data on where a polygon crosses the scanline. Only the Crossing with lowest Crossing::x and highest are recorded.
     */
    struct PolyCrossings
    {
        unsigned int poly_idx; //!< The index of the polygon which crosses the scanline
        Crossing min; //!< The point where the polygon first crosses the scanline.
        Crossing max; //!< The point where the polygon last crosses the scanline.
        /*!
         * Create a PolyCrossings with minimal initialization. PolyCrossings::min and PolyCrossings::max are not yet computed.
         * \param poly_idx The index of the polygon in LinePolygonsCrossings::boundary
         */
        PolyCrossings(unsigned int poly_idx) 
        : poly_idx(poly_idx)
        , min(INT64_MAX, NO_INDEX), max(INT64_MIN, NO_INDEX) 
        { 
        }
    };

    /*!
     * A PolyCrossings list: for every polygon a PolyCrossings.
     */
    struct PartCrossings : public std::vector<PolyCrossings>
    {
        //unsigned int part_idx;
    };
    
    
    PartCrossings crossings; //!< All crossings of polygons in the LinePolygonsCrossings::boundary with the scanline.
    unsigned int min_crossing_idx; //!< The index into LinePolygonsCrossings::crossings to the crossing with the minimal PolyCrossings::min crossing of all PolyCrossings's.
    unsigned int max_crossing_idx; //!< The index into LinePolygonsCrossings::crossings to the crossing with the maximal PolyCrossings::max crossing of all PolyCrossings's.
    
    Polygons& boundary; //!< The boundary not to cross during combing.
    Point startPoint; //!< The start point of the scanline.
    Point endPoint; //!< The end point of the scanline.
    
    int64_t dist_to_move_boundary_point_outside; //!< The distance used to move outside or inside so that a boundary point doesn't intersect with the boundary anymore. Neccesary due to computational rounding problems. Use negative value for insicde combing.
    
    PointMatrix transformation_matrix; //!< The transformation which rotates everything such that the scanline is aligned with the x-axis.
    Point transformed_startPoint; //!< The LinePolygonsCrossings::startPoint as transformed by Comb::transformation_matrix
    Point transformed_endPoint; //!< The LinePolygonsCrossings::endPoint as transformed by Comb::transformation_matrix

    
    /*!
     * Check if we are crossing the boundaries, and pre-calculate some values.
     * 
     * Sets Comb::transformation_matrix, Comb::transformed_startPoint and Comb::transformed_endPoint
     * \return Whether the line segment from LinePolygonsCrossings::startPoint to LinePolygonsCrossings::endPoint collides with the boundary
     */
    bool lineSegmentCollidesWithBoundary();
    
    /*!
     * Calculate Comb::crossings, Comb::min_crossing_idx and Comb::max_crossing_idx.
     */
    void calcScanlineCrossings();
    
    /*! 
     * Get the basic combing path and optimize it.
     * 
     * \param combPath Output parameter: the points along the combing path.
     */
    void getCombingPath(CombPath& combPath, int64_t max_comb_distance_ignored = MM2INT(1.5));
    
    /*! 
     * Get the basic combing path, without shortcuts. The path goes straight toward the endPoint and follows the boundary when it hits it, until it passes the scanline again.
     * 
     * Walk trough the crossings, for every boundary we cross, find the initial cross point and the exit point. Then add all the points in between
     * to the \p combPath and continue with the next boundary we will cross, until there are no more boundaries to cross.
     * This gives a path from the start to finish curved around the holes that it encounters.
     * 
     * \param combPath Output parameter: the points along the combing path.
     */
    void getBasicCombingPath(CombPath& combPath);
    
    /*! 
     * Get the basic combing path, following a single boundary polygon when it hits it, until it passes the scanline again.
     * 
     * Find the initial cross point and the exit point. Then add all the points in between
     * to the \p combPath and continue with the next boundary we will cross, until there are no more boundaries to cross.
     * This gives a path from the start to finish curved around the polygon that it encounters.
     * 
     * \param combPath Output parameter: where to add the points along the combing path.
     */
    void getBasicCombingPath(PolyCrossings& crossings, CombPath& combPath);
    
    /*!
     * Find the first polygon cutting the scanline after \p x.
     * 
     * Note that this function only looks at the first segment cutting the scanline (see Comb::minX)!
     * It doesn't return the next polygon which crosses the scanline, but the first polygon crossing the scanline for the first time.
     * 
     * \param x The point on the scanline from where to look.
     * \return The next PolyCrossings fully beyond \p x or one with PolyCrossings::poly_idx set to NO_INDEX if there's none left.
     */
    PolyCrossings* getNextPolygonAlongScanline(int64_t x);
    
    /*!
     * Optimize the \p comb_path: skip each point we could already reach by not crossing a boundary. This smooths out the path and makes it skip some unneeded corners.
     * 
     * \param comb_path The unoptimized combing path.
     * \param optimized_comb_path Output parameter: The points of optimized combing path
     * \return Whether it turns out that the basic comb path already crossed a boundary
     */
    bool optimizePath(CombPath& comb_path, CombPath& optimized_comb_path);
    
    /*!
     * Create a LinePolygonsCrossings with minimal initialization.
     * \param boundary The boundary which not to cross during combing
     * \param start the starting point
     * \param end the end point
     * \param dist_to_move_boundary_point_outside Distance used to move a point from a boundary so that it doesn't intersect with it anymore. (Precision issue)
     */
    LinePolygonsCrossings(Polygons& boundary, Point& start, Point& end, int64_t dist_to_move_boundary_point_outside)
    : boundary(boundary), startPoint(start), endPoint(end), dist_to_move_boundary_point_outside(dist_to_move_boundary_point_outside)
    {
    }
    
public: 
    
    /*!
     * The main function of this class: calculate one combing path within the boundary.
     * \param boundary The polygons to follow when calculating the basic combing path
     * \param startPoint From where to start the combing move.
     * \param endPoint Where to end the combing move.
     * \param combPath Output parameter: the combing path generated.
     */
    static void comb(Polygons& boundary, Point startPoint, Point endPoint, CombPath& combPath, int64_t dist_to_move_boundary_point_outside, int64_t max_comb_distance_ignored = MM2INT(1.5))
    {
        LinePolygonsCrossings linePolygonsCrossings(boundary, startPoint, endPoint, dist_to_move_boundary_point_outside);
        linePolygonsCrossings.getCombingPath(combPath, max_comb_distance_ignored);
    };
};

class SliceDataStorage;

/*!
 * Class for generating a full combing actions from a travel move from a start point to an end point.
 * A single Comb object is used for each layer.
 * 
 * Comb::calc is the main function of this class.
 * 
 * Typical output: A combing path to the boundary of the polygon + a move through air avoiding other parts in the layer + a combing path from the boundary of the ending polygon to the end point.
 * Each of these three is a CombPath; the first and last are within Comb::boundary_inside while the middle is outside of Comb::boundary_outside.
 * Between these there is a little gap where the nozzle crosses the boundary of an object approximately perpendicular to its boundary.
 * 
 * As an optimization, the combing paths inside are calculated on specifically those PolygonsParts within which to comb, while the coundary_outside isn't split into outside parts, 
 * because generally there is only one outside part; encapsulated holes occur less often.
 */
class Comb 
{
    friend class LinePolygonsCrossings;
private:
    SliceDataStorage& storage; //!< The storage from which to compute the outside boundary, when needed.
    int layer_nr; //!< The layer number for the layer for which to compute the outside boundary, when needed.
    
    int64_t offset_from_outlines; //!< Offset from the boundary of a part to the comb path. (nozzle width / 2)
    int64_t max_moveInside_distance2; //!< Maximal distance of a point to the Comb::boundary_inside which is still to be considered inside. (very sharp corners not allowed :S)
    int64_t offset_from_outlines_outside; //!< Offset from the boundary of a part to a travel path which avoids it by this distance.
    static const int64_t max_moveOutside_distance2 = INT64_MAX; //!< Any point which is not inside should be considered outside.
    static const int64_t offset_dist_to_get_from_on_the_polygon_to_outside = 40; //!< in order to prevent on-boundary vs crossing boundary confusions (precision thing)
    static const int64_t offset_extra_start_end = 100; //!< Distance to move start point and end point toward eachother to extra avoid collision with the boundaries.
    
    bool avoid_other_parts; //!< Whether to perform inverse combing a.k.a. avoid parts.
    
    Polygons& boundary_inside; //!< The boundary within which to comb.
    Polygons* boundary_outside; //!< The boundary outside of which to stay to avoid collision with other layer parts. This is a pointer cause we only compute it when we move outside the boundary (so not when there is only a single part in the layer)
    PartsView partsView_inside; //!< Structured indices onto boundary_inside which shows which polygons belong to which part. 

    /*!
     * Get the boundary_outside, which is an offset from the outlines of all meshes in the layer. Calculate it when it hasn't been calculated yet.
     */
    Polygons* getBoundaryOutside();
    
public:
    /*!
     * Initializes the combing areas for every mesh in the layer (not support)
     * \param storage Where the layer polygon data is stored
     * \param layer_nr The number of the layer for which to generate the combing areas.
     * \param comb_boundary_inside The comb boundary within which to comb within layer parts.
     * \param offset_from_outlines The offset from the outline polygon, to create the combing boundary in case there is no second wall.
     * \param travel_avoid_other_parts Whether to avoid other layer parts when traveling through air.
     * \param travel_avoid_distance The distance by which to avoid other layer parts when traveling through air.
     */
    Comb(SliceDataStorage& storage, int layer_nr, Polygons& comb_boundary_inside, int64_t offset_from_outlines, bool travel_avoid_other_parts, int64_t travel_avoid_distance);
    
    ~Comb();

    /*!
     * Calculate the comb paths (if any) - one for each polygon combed alternated with travel paths
     * 
     * \param startPoint Where to start moving from
     * \param endPoint Where to move to
     * \param combPoints Output parameter: The points along the combing path, excluding the \p startPoint (?) and \p endPoint
     * \param startInside Whether we want to start inside the comb boundary
     * \param endInside Whether we want to end up inside the comb boundary
     * \return Whether combing has succeeded; otherwise a retraction is needed.
     */    
    bool calc(Point startPoint, Point endPoint, CombPaths& combPaths, bool startInside = false, bool endInside = false, int64_t max_comb_distance_ignored = MM2INT(1.5));
};

}//namespace cura

#endif//COMB_H
