/*
 *  This file is part of AQUA-gpusph, a free CFD program based on SPH.
 *  Copyright (C) 2012  Jose Luis Cercos Pita <jl.cercos@upm.es>
 *
 *  AQUA-gpusph is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  AQUA-gpusph is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with AQUA-gpusph.  If not, see <http://www.gnu.org/licenses/>.
 */

// Artificial viscosity factor
#ifndef __CLEARY__
	#ifndef HAVE_3D
		#define __CLEARY__ 8.f
	#else
		#define __CLEARY__ 15.f
	#endif
#endif

// ------------------------------------------------------------------
// Study if two particles can interact
// ------------------------------------------------------------------
if(!iMove[i]){
	i++;
	continue;
}
#if __BOUNDARY__==0 || __BOUNDARY__==2
	// ElasticBounce or DeLeffe boundary condition
	if(iMove[i]<0){
		i++;
		continue;
	}
#endif
hav = 0.5f*(iHp + hp[i]);                                       // Average kernel heights for interactions              [m]
dist = sep*hav;                                                 // Maximum interaction distance                         [m]
r = iPos - pos[i];
r1  = fast_length(r);                                           // Distance between particles                           [m]
if( r1 <= dist )
{
	#ifndef HAVE_3D
		conw = 1.f/(hav*hav);
		conf = 1.f/(hav*hav*hav*hav);
	#else
		conw = 1.f/(hav*hav*hav);
		conf = 1.f/(hav*hav*hav*hav*hav);
	#endif
	dv   = iV - v[i];                                       // Delta of velocity                                    [m/s]
	vdr  = dot(dv, r);
	//---------------------------------------------------------------
	//       calculate the kernel wab and the function fab
	//---------------------------------------------------------------
	pDens = dens[i];                                        // Density of neighbour particle                        [kg/m3]
	pMass = pmass[i];                                       // Mass of neighbour particle                           [kg]
	wab = kernelW(r1/hav)*conw*pMass;
	fab = kernelF(r1/hav)*conf*pMass;
	//---------------------------------------------------------------
	//       calculate the pressure factor
	//---------------------------------------------------------------
	prfac = iPress/(iDens*iDens) + press[i]/(pDens*pDens);
	if(iMove[i]<0){	// Fix particles can't substract fluid particles
		prfac = max(prfac, 0.f);
	}
	//---------------------------------------------------------------
	//       calculate viscosity terms (Cleary's viscosity)
	//---------------------------------------------------------------
	viscg = -__CLEARY__*iViscdyn*
			vdr / (r1*r1 + 0.01f*hav*hav)
			/(pDens*iDens);
	#ifdef __FREE_SLIP__
		if(iMove[i]<0)
			viscg = 0.f;
	#endif
	_SIGMA_ = max(_SIGMA_, hav*hav/iVisckin);
	//---------------------------------------------------------------
	//       force computation
	//---------------------------------------------------------------
	_F_ -= r*fab*(prfac + viscg);                           // Particle force                                       [m/s2]
	//---------------------------------------------------------------
	// 	rate of change of density
	//---------------------------------------------------------------
	_DRDT_ += vdr*fab;                                      // Density rate                                         [kg/m3/s]
	//---------------------------------------------------------------
	//       Density diffusion term
	//---------------------------------------------------------------
	#ifdef __DELTA_SPH__
		// Ferrari
		/*
		rdr = dot( r, r ) / (r1 + 0.01f*hav);
		drfac = (iPress - press[i]) - rDens*dot(grav,r);
		_DRDT_F_ += iDelta / (cs*pDens)
		            * ( drfac * rdr ) * fab;
		*/
		// Molteni
		/*
		rdr = dot( r, r ) / (r1*r1 + 0.01f*hav*hav);
		drfac = (iPress - press[i]) - rDens*dot(grav,r);
		_DRDT_F_ += 2.f*iDelta*hav / (cs*pDens)
		            * ( drfac * rdr ) * fab;
		*/
		// Cercos
		rdr = dot( r, r ) / (r1*r1 + 0.01f*hav*hav);
		drfac = (iPress - press[i]) - rDens*dot(grav,r);
		_DRDT_F_ += iDelta*dt * iDens/(rDens*pDens)
		            * ( drfac * rdr ) * fab;
	#endif
	//---------------------------------------------------------------
	// 	Shepard term
	//---------------------------------------------------------------
	_SHEPARD_ += wab/pDens;                                 // Kernel integral
	//---------------------------------------------------------------
	//       Shepard term gradient
	//---------------------------------------------------------------
	_GRADW_  -= r*fab/pDens;                                // Kernel integral gradient                             [1/m]
}