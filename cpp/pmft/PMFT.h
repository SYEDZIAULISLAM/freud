// Copyright (c) 2010-2019 The Regents of the University of Michigan
// This file is from the freud project, released under the BSD 3-Clause License.

#ifndef PMFT_H
#define PMFT_H

#include <memory>
#include <tbb/tbb.h>

#include "Box.h"
#include "Histogram.h"
#include "ManagedArray.h"
#include "NeighborComputeFunctional.h"
#include "VectorMath.h"

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
    PMFT() : m_box(box::Box()), m_frame_counter(0), m_n_points(0) , m_n_query_points(0), m_reduce(true) {}

    //! Destructor
    virtual ~PMFT() {};

    //! Reset the PCF array to all zeros
    void reset()
    {
        m_local_histograms.reset();
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

    //! Get bin centers.
    std::vector<std::vector<float> > getBinCenters() const
    {
        // RDFs are always 1D histograms, so we just return the first element.
        return m_histogram.getBinCenters();
    }

    //! Return the bin boundaries.
    std::vector<std::vector<float> > getBinEdges() const
    {
        // RDFs are always 1D histograms, so we just return the first element.
        return m_histogram.getBinEdges();
    }


    //! Return the bin boundaries.
    std::vector<std::pair<float, float> > getBounds() const
    {
        // RDFs are always 1D histograms, so we just return the first element.
        return m_histogram.getBounds();
    }

    //! Return the bin boundaries.
    std::vector<unsigned int> getBinSizes() const
    {
        // RDFs are always 1D histograms, so we just return the first element.
        return m_histogram.getBinSizes();
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

    //! Helper function to reduce three dimensionally with appropriate Jacobian.
    template<typename JacobFactor>
    void reduce(JacobFactor jf)
    {
        m_pcf_array.prepare(m_histogram.shape());
        m_histogram.reset();

        float inv_num_dens = m_box.getVolume() / (float) m_n_query_points;
        float norm_factor = (float) 1.0 / ((float) m_frame_counter * (float) m_n_points);
        float prefactor = inv_num_dens*norm_factor;

        m_histogram.reduceOverThreadsPerBin(m_local_histograms,
                [this, &prefactor, &jf] (size_t i) {
                m_pcf_array[i] = m_histogram[i] * prefactor * jf(i);
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
        return reduceAndReturn(m_histogram.getBinCounts());
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

    util::ManagedArray<float> m_pcf_array;         //!< Array of computed pair correlation function.
    util::Histogram m_histogram; //!< Counts for each bin.
    util::Histogram::ThreadLocalHistogram m_local_histograms;   //!< Thread local bin counts for TBB parallelism
};

}; }; // end namespace freud::pmft

#endif // PMFT_H
