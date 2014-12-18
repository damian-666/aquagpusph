/*
 *  This file is part of AQUAgpusph, a free CFD program based on SPH.
 *  Copyright (C) 2012  Jose Luis Cercos Pita <jl.cercos@upm.es>
 *
 *  AQUAgpusph is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  AQUAgpusph is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with AQUAgpusph.  If not, see <http://www.gnu.org/licenses/>.
 */

/** @file
 * @brief Particles sorting/unsorting tool.
 * (See Aqua::CalcServer::Permutate for details)
 */

#ifndef PERMUTATE_H_INCLUDED
#define PERMUTATE_H_INCLUDED

#include <CalcServer/Kernel.h>
#include <deque>

namespace Aqua{ namespace CalcServer{

/** @class Permutate Permutate.h CalcServer/Permutate.h
 * @brief Particles sorting/unsorting tool.
 *
 * Permute the data to sort it before the interactions stage, restoring it
 * later.
 *
 * @see Permutate.cl
 */
class Permutate : public Aqua::CalcServer::Kernel
{
public:
    /// Constructor.
    Permutate();

    /// Destructor.
    ~Permutate();

    /** @brief Sort the particles.
     * @return false if all gone right, true otherwise.
     */
    bool sort();

    /** @brief Unsort the particles.
     * @return false if all gone right, true otherwise.
     */
    bool unsort();

private:
    /** @brief Run the permutations.
     * @param permutations Permutations memory object
     * @return false if all gone right, true otherwise.
     */
    bool execute(cl_mem permutations);

    /** @brief Set the origin and duplicated OpenCL memory objects
     * @return false if all gone right, true otherwise.
     */
    bool setupMems();

    /** @brief Setup the OpenCL stuff
     * @return false if all gone right, true otherwise.
     */
    bool setupOpenCL();

    /// OpenCL script path
    char *_path;

    /// OpenCL program
    cl_program _program;
    /// Data sorting kernel
    cl_kernel _kernel;

    /// Origin buffers
    std::deque<cl_mem> _mems;
    /// Origin buffers sizes
    std::deque<size_t> _mems_size;
    /// Duplicated buffers
    std::deque<cl_mem> _backup;

    /// Global work size.
    size_t _global_work_size;
    /// Local work size
    size_t _local_work_size;

};

}}  // namespace

#endif // PERMUTATE_H_INCLUDED