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

// ------------------------------------------------------------------
// Get type of sensor
// ------------------------------------------------------------------
ushort mode = sensorMode[labp - n];
if(mode == 0){
	// Values must be interpolated as ussual
	#include "RatesBounds.hcl"
	i++;
	continue;
}
// If this point is reached then maximum value mode is activated.

// ------------------------------------------------------------------
// Study if two particles can interact
// ------------------------------------------------------------------
if(!iMove[i]){
	i++;
	continue;
}
#if __BOUNDARY__==2 || __BOUNDARY__==0
	if(iMove[i]<0){
		i++;
		continue;
	}
#endif
dist = sep*h;                                // Maximum interaction distance               [m]
r = iPos - pos[i];
r1  = fast_length(r);                          // Distance between particles                 [m]
if( r1 <= dist )
{
	#ifndef HAVE_3D
		conw = 1.f/(h*h);                  // Different for 1d and 3d
		conf = 1.f/(h*h*h*h);          // Different for 1d and 3d
	#else
		conw = 1.f/(h*h*h);              // Different for 1d and 3d
		conf = 1.f/(h*h*h*h*h);      // Different for 1d and 3d
	#endif
	//---------------------------------------------------------------
	//       calculate the kernel wab and the function fab
	//---------------------------------------------------------------
	pDens = dens[i];                       // Density of neighbour particle              [kg/m3]
	wab = kernelW(r1/h)*conw*pMass;
	fab = kernelF(r1/h)*conf*pMass;
	//---------------------------------------------------------------
	//       pressure computation (stored on f)
	//---------------------------------------------------------------
	_F_ = max(_F_, press[i]);              // Detect maximum nearby pressure             [Pa]
	//---------------------------------------------------------------
	// 	density computation (stored on drdt)
	//---------------------------------------------------------------
	_DRDT_ = max(_DRDT_, pDens);           // Detect maximum nearby density              [kg/m3]
	//---------------------------------------------------------------
	// 	Shepard term
	//---------------------------------------------------------------
	_SHEPARD_ += wab/pDens;
}
