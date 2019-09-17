// Copyright (c) 2010-2019 The Regents of the University of Michigan
// This file is from the freud project, released under the BSD 3-Clause License.

#ifndef PMFT_H
#define PMFT_H

#include <memory>
#include <ostream>
#include <tbb/tbb.h>

#include "Box.h"
#include "Index1D.h"
#include "NeighborList.h"
#include "VectorMath.h"
#include "ThreadStorage.h"
#include "ManagedArray.h"
#include "NeighborQuery.h"
#include "NeighborComputeFunctional.h"

/*! \internal
    \file PMFT.h
    \brief Declares base class for all PMFT classes
*/

namespace freud { namespace pmft {

//! Computes the PMFT for a given set of points
/*! The PMFT class is an abstract class providing the basis for all classes calculating PMFTs for specific
 *  dimensional cases. The PMFT class defines some of the key interfaces required for all PMFT classes, such
 *  as the ability to access the underlying PCF and box. Many of the specific methods must be implemented by
 *  subclasses that account for the proper set of dimensions.The required functions are implemented as pure
 *  virtual functions here to enforce this.
 */
class PMFT
{
public:
    //! Constructor
    PMFT() : m_box(box::Box()), m_frame_counter(0), m_n_points(0) , m_n_query_points(0), m_reduce(true), m_r_max(0) {}

    //! Destructor
    virtual ~PMFT() {};

    //! Reset the PCF array to all zeros
    void reset()
    {
        m_local_bin_counts.reset();
        this->m_frame_counter = 0;
        this->m_reduce = true;
    }

    //! \internal
    //! helper function to reduce the thread specific arrays into one array
    //! Must be implemented by subclasses
    virtual void reducePCF() = 0;

    //! Implementing pure virtual function from parent class.
    virtual void reduce()
    {
        reducePCF();
    }

    float getRMax()
    {
        return m_r_max;
    }

    //! \internal
    // Wrapper to do accumulation.
    /*! \param neighbor_query NeighborQuery object to iterate over
        \param query_points Points
        \param n_query_points Number of query_points
        \param nlist Neighbor List. If not NULL, loop over it. Otherwise, use neighbor_query
           appropriately with given qargs.
        \param qargs Query arguments
        \param cf An object with operator(NeighborBond) as input.
    */
    template<typename Func>
    void accumulateGeneral(const locality::NeighborQuery* neighbor_query, 
                           const vec3<float>* query_points, unsigned int n_query_points,
                           const locality::NeighborList* nlist,
                           freud::locality::QueryArgs qargs,
                           Func cf)
    {
        m_box = neighbor_query->getBox();
        locality::loopOverNeighbors(neighbor_query, query_points, n_query_points, qargs, nlist, cf);
        m_frame_counter++;
        m_n_points = neighbor_query->getNPoints();
        m_n_query_points = n_query_points;
        // flag to reduce
        m_reduce = true;
    }

    //! Helper function to precompute axis bin center,
    util::ManagedArray<float> precomputeAxisBinCenter(unsigned int size, float d, float max)
    {
        return precomputeArrayGeneral(size, d, [=](float T, float nextT) { return -max + ((T + nextT) / 2.0); });
    }

    //! Helper function to precompute array with the following logic.
    //! :code:`Func cf` should be some sort of (float)(float, float).
    template<typename Func>
    util::ManagedArray<float> precomputeArrayGeneral(unsigned int size, float d, Func cf)
    {
        util::ManagedArray<float> arr({size});
        for (unsigned int i = 0; i < size; i++)
        {
            float T = float(i) * d ;
            float nextT = float(i + 1) * d;
            arr[i] = cf(T, nextT);
        }
        return arr;
    }

    //! Helper function to reduce two dimensionally with appropriate Jaocobian.
    template<typename JacobFactor>
    void reduce2D(unsigned int first_dim, unsigned int second_dim, JacobFactor jf)
    {
        reduce3D(1, first_dim, second_dim, jf);
    }

    //! Helper function to reduce three dimensionally with appropriate Jaocobian.
    template<typename JacobFactor>
    void reduce3D(unsigned int n_r, unsigned int first_dim, unsigned int second_dim, JacobFactor jf)
    {
        unsigned int local_bin_counts_size = n_r * first_dim * second_dim;
        m_bin_counts.prepare({first_dim, second_dim, n_r});
        m_pcf_array.prepare({first_dim, second_dim, n_r});
        parallel_for(tbb::blocked_range<size_t>(0, local_bin_counts_size),
                     [=](const tbb::blocked_range<size_t>& r) {
                         for (size_t i = r.begin(); i != r.end(); i++)
                         {
                             for (util::ThreadStorage<unsigned int>::const_iterator local_bins
                                  = m_local_bin_counts.begin();
                                  local_bins != m_local_bin_counts.end(); ++local_bins)
                             {
                                 m_bin_counts[i] += (*local_bins)[i];
                             }
                         }
                     });
        float inv_num_dens = m_box.getVolume() / (float) m_n_query_points;
        float norm_factor = (float) 1.0 / ((float) m_frame_counter * (float) m_n_points);
        // normalize pcf_array
        // avoid need to unravel b/c arrays are in the same index order
        parallel_for(tbb::blocked_range<size_t>(0, n_r * first_dim * second_dim),
                     [=](const tbb::blocked_range<size_t>& r) {
                         for (size_t i = r.begin(); i != r.end(); i++)
                         {
                             m_pcf_array[i]
                                 = (float) m_bin_counts[i] * norm_factor * jf(i) * inv_num_dens;
                         }
                     });
    }

    //! Get a reference to the PCF array
    const util::ManagedArray<float> &getPCF()
    {
        return reduceAndReturn(m_pcf_array);
    }

    //! Get a reference to the bin counts array
    const util::ManagedArray<unsigned int> &getBinCounts()
    {
        return reduceAndReturn(m_bin_counts);
    }

    //! Return :code:`thing_to_return` after reducing.
    template<typename T>
    T &reduceAndReturn(T &thing_to_return)
    {
        if (m_reduce == true)
        {
            reduce();
        }
        m_reduce = false;
        return thing_to_return;
    }

    //! Get the simulation box
    const box::Box& getBox() const
    {
        return m_box;
    }

protected:
    box::Box m_box;
    unsigned int m_frame_counter;    //!< Number of frames calculated.
    unsigned int m_n_points;         //!< The number of points.
    unsigned int m_n_query_points;   //!< The number of query points.
    bool m_reduce;                   //!< Whether or not the histogram needs to be reduced.
    float m_r_max; //!< r_max used in cell list construction

    util::ManagedArray<float> m_pcf_array;         //!< Array of computed pair correlation function.
    util::ManagedArray<unsigned int> m_bin_counts; //!< Counts for each bin.
    util::ThreadStorage<unsigned int> m_local_bin_counts;   //!< Thread local bin counts for TBB parallelism
};

}; }; // end namespace freud::pmft

#endif // PMFT_H
