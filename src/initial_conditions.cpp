/*! \file initial_conditions.cpp
 *  \brief Definitions of initial conditions for different tests.
           Note that the grid is mapped to 1D as i + (x_dim)*j + (x_dim*y_dim)*k.
           Functions are members of the Grid3D class. */


#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "global.h"
#include "grid3D.h"
#include "mpi_routines.h"
#include "io.h"
#include "error_handling.h"
#include <stdio.h>

/*! \fn void Set_Initial_Conditions(parameters P)
 *  \brief Set the initial conditions based on info in the parameters structure. */
void Grid3D::Set_Initial_Conditions(parameters P, Real C_cfl) {

  Set_Domain_Properties(P);
  Set_Gammas(P.gamma);

  if (strcmp(P.init, "Constant")==0) {
    Constant(P.rho, P.vx, P.vy, P.vz, P.P);    
  } else if (strcmp(P.init, "Sound_Wave")==0) {
    Sound_Wave(P.rho, P.vx, P.vy, P.vz, P.P, P.A);
  } else if (strcmp(P.init, "Square_Wave")==0) {
    Square_Wave(P.rho, P.vx, P.vy, P.vz, P.P, P.A);    
  } else if (strcmp(P.init, "Riemann")==0) {
    Riemann(P.rho_l, P.v_l, P.P_l, P.rho_r, P.v_r, P.P_r, P.diaph);
  } else if (strcmp(P.init, "Shu_Osher")==0) {
    Shu_Osher();
  } else if (strcmp(P.init, "Blast_1D")==0) {
    Blast_1D();
  } else if (strcmp(P.init, "KH_discontinuous_2D")==0) {
    KH_discontinuous_2D();
  } else if (strcmp(P.init, "KH_res_ind_2D")==0) {
    KH_res_ind_2D();
  } else if (strcmp(P.init, "Implosion_2D")==0) {
    Implosion_2D();
  } else if (strcmp(P.init, "Noh_2D")==0) {
    Noh_2D();
  } else if (strcmp(P.init, "Noh_3D")==0) {
    Noh_3D();    
  } else if (strcmp(P.init, "Read_Grid")==0) {
    Read_Grid(P);    
  } else {
    chprintf ("ABORT: %s: Unknown initial conditions!\n", P.init);
    chexit(0);
  }

}

/*! \fn void Set_Domain_Properties(struct parameters P)
 *  \brief Set local domain properties */
void Grid3D::Set_Domain_Properties(struct parameters P)
{
#ifndef  MPI_CHOLLA
  H.xbound = P.xmin;
  H.ybound = P.ymin;
  H.zbound = P.zmin;

  /*perform 1-D first*/
  if(H.nx > 1 && H.ny==1 && H.nz==1)
  {
    H.domlen_x =  P.xlen;
    H.domlen_y =  P.ylen / (H.nx - 2*H.n_ghost);
    H.domlen_z =  P.zlen / (H.nx - 2*H.n_ghost);
    H.dx = H.domlen_x / (H.nx - 2*H.n_ghost);
    H.dy = H.domlen_y;
    H.dz = H.domlen_z;
  }

  /*perform 2-D next*/
  if(H.nx > 1 && H.ny>1 && H.nz==1)
  {
    H.domlen_x =  P.xlen;
    H.domlen_y =  P.ylen;
    H.domlen_z =  P.zlen / (H.nx - 2*H.n_ghost);
    H.dx = H.domlen_x / (H.nx - 2*H.n_ghost);
    H.dy = H.domlen_y / (H.ny - 2*H.n_ghost);
    H.dz = H.domlen_z;
  }

  /*perform 3-D last*/
  if(H.nx>1 && H.ny>1 && H.nz>1)
  {
    H.domlen_x = P.xlen;
    H.domlen_y = P.ylen;
    H.domlen_z = P.zlen;
    H.dx = H.domlen_x / (H.nx - 2*H.n_ghost);
    H.dy = H.domlen_y / (H.ny - 2*H.n_ghost);
    H.dz = H.domlen_z / (H.nz - 2*H.n_ghost);
  }

  /*set MPI variables (same as local for non-MPI)*/
  H.xblocal = H.xbound;
  H.yblocal = H.ybound;
  H.zblocal = H.zbound;
  H.xdglobal = H.domlen_x;
  H.ydglobal = H.domlen_y;
  H.zdglobal = H.domlen_z;

#else  /*MPI_CHOLLA*/

  /* set the local domains on each process */
  Set_Parallel_Domain(P.xmin, P.ymin, P.zmin, P.xlen, P.ylen, P.zlen, &H);

#endif /*MPI_CHOLLA*/
}



/*! \fn void Constant(Real rho, Real vx, Real vy, Real vz, Real P)
 *  \brief Constant gas properties. */
void Grid3D::Constant(Real rho, Real vx, Real vy, Real vz, Real P)
{
  int i, j, k, id;
  int istart, jstart, kstart, iend, jend, kend;
  Real x_pos, y_pos, z_pos;

  istart = H.n_ghost;
  iend   = H.nx-H.n_ghost;
  if (H.ny > 1) {
    jstart = H.n_ghost;
    jend   = H.ny-H.n_ghost;
  }
  else {
    jstart = 0;
    jend   = H.ny;
  }
  if (H.nz > 1) {
    kstart = H.n_ghost;
    kend   = H.nz-H.n_ghost;
  }
  else {
    kstart = 0;
    kend   = H.nz;
  }

  // set initial values of conserved variables
  for(k=kstart; k<kend; k++) {
    for(j=jstart; j<jend; j++) {
      for(i=istart; i<iend; i++) {

        //get cell index
        id = i + j*H.nx + k*H.nx*H.ny;

        // get cell-centered position
        Get_Position(i, j, k, &x_pos, &y_pos, &z_pos);
        
        // set constant initial states
        C.density[id]    = rho;
        C.momentum_x[id] = rho*vx;
        C.momentum_y[id] = rho*vy;
        C.momentum_z[id] = rho*vz;
        C.Energy[id]     = P/(gama-1.0) + 0.5*rho*(vx*vx + vy*vy + vz*vz);
        #ifdef DE
        C.GasEnergy[id]  = P/(gama-1.0);
        #endif
      }
    }
  }

}


/*! \fn void Sound_Wave(Real rho, Real vx, Real vy, Real vz, Real P, Real A)
 *  \brief Sine wave perturbation. */
void Grid3D::Sound_Wave(Real rho, Real vx, Real vy, Real vz, Real P, Real A)
{
  int i, j, k, id;
  int istart, jstart, kstart, iend, jend, kend;
  Real x_pos, y_pos, z_pos;

  istart = H.n_ghost;
  iend   = H.nx-H.n_ghost;
  if (H.ny > 1) {
    jstart = H.n_ghost;
    jend   = H.ny-H.n_ghost;
  }
  else {
    jstart = 0;
    jend   = H.ny;
  }
  if (H.nz > 1) {
    kstart = H.n_ghost;
    kend   = H.nz-H.n_ghost;
  }
  else {
    kstart = 0;
    kend   = H.nz;
  }

  // set initial values of conserved variables
  for(k=kstart; k<kend; k++) {
    for(j=jstart; j<jend; j++) {
      for(i=istart; i<iend; i++) {

        //get cell index
        id = i + j*H.nx + k*H.nx*H.ny;

        // get cell-centered position
        Get_Position(i, j, k, &x_pos, &y_pos, &z_pos);
        
        // set constant initial states
        C.density[id]    = rho;
        C.momentum_x[id] = rho*vx;
        C.momentum_y[id] = rho*vy;
        C.momentum_z[id] = rho*vz;
        C.Energy[id]     = P/(gama-1.0) + 0.5*rho*(vx*vx + vy*vy + vz*vz);
        // add small-amplitude perturbations
        C.density[id]    = C.density[id]    + A * sin(2.0*PI*x_pos);
        C.momentum_x[id] = C.momentum_x[id] + A * sin(2.0*PI*x_pos);
        C.momentum_y[id] = C.momentum_y[id] + A * sin(2.0*PI*x_pos);
        C.momentum_z[id] = C.momentum_z[id] + A * sin(2.0*PI*x_pos);
        C.Energy[id]     = C.Energy[id]     + A * (1.5) * sin(2*PI*x_pos);
      }
    }
  }

}


/*! \fn void Square_Wave(Real rho, Real vx, Real vy, Real vz, Real P, Real A)
 *  \brief Square wave density perturbation with amplitude A*rho in pressure equilibrium. */
void Grid3D::Square_Wave(Real rho, Real vx, Real vy, Real vz, Real P, Real A)
{
  int i, j, k, id;
  int istart, jstart, kstart, iend, jend, kend;
  Real x_pos, y_pos, z_pos;

  istart = H.n_ghost;
  iend   = H.nx-H.n_ghost;
  if (H.ny > 1) {
    jstart = H.n_ghost;
    jend   = H.ny-H.n_ghost;
  }
  else {
    jstart = 0;
    jend   = H.ny;
  }
  if (H.nz > 1) {
    kstart = H.n_ghost;
    kend   = H.nz-H.n_ghost;
  }
  else {
    kstart = 0;
    kend   = H.nz;
  }

  // set initial values of conserved variables
  for(k=kstart; k<kend; k++) {
    for(j=jstart; j<jend; j++) {
      for(i=istart; i<iend; i++) {

        //get cell index
        id = i + j*H.nx + k*H.nx*H.ny;

        // get cell-centered position
        Get_Position(i, j, k, &x_pos, &y_pos, &z_pos);

        C.density[id]    = rho;
        //C.momentum_x[id] = 0.0;
        C.momentum_x[id] = rho * vx;
        C.momentum_y[id] = rho * vy;
        C.momentum_z[id] = rho * vz;
        //C.momentum_z[id] = rho_l * v_l;
        C.Energy[id]     = P/(gama-1.0) + 0.5*rho*(vx*vx + vy*vy + vz*vz);
        #ifdef DE
        C.GasEnergy[id]  = P/(gama-1.0);
        #endif
        if (x_pos > 0.25*H.domlen_x && x_pos < 0.75*H.domlen_x)
        {
          C.density[id]    = rho*A;
          C.momentum_x[id] = rho*A * vx;
          C.momentum_y[id] = rho*A * vy;
          C.momentum_z[id] = rho*A * vz;
          C.Energy[id]     = P/(gama-1.0) + 0.5*rho*A*(vx*vx + vy*vy + vz*vz);        
          #ifdef DE
          C.GasEnergy[id]  = P/(gama-1.0);
          #endif
        }
      }
    }
  }
}


/*! \fn void Riemann(Real rho_l, Real v_l, Real P_l, Real rho_r, Real v_r, Real P_r, Real diaph)
 *  \brief Initialize the grid with a Riemann problem. */
void Grid3D::Riemann(Real rho_l, Real v_l, Real P_l, Real rho_r, Real v_r, Real P_r, Real diaph)
{
  int i, j, k, id;
  int istart, jstart, kstart, iend, jend, kend;
  Real x_pos, y_pos, z_pos;
  Real v, P, cs;

  istart = H.n_ghost;
  iend   = H.nx-H.n_ghost;
  if (H.ny > 1) {
    jstart = H.n_ghost;
    jend   = H.ny-H.n_ghost;
  }
  else {
    jstart = 0;
    jend   = H.ny;
  }
  if (H.nz > 1) {
    kstart = H.n_ghost;
    kend   = H.nz-H.n_ghost;
  }
  else {
    kstart = 0;
    kend   = H.nz;
  }

  // set initial values of conserved variables
  for(k=kstart; k<kend; k++) {
    for(j=jstart; j<jend; j++) {
      for(i=istart; i<iend; i++) {

        //get cell index
        id = i + j*H.nx + k*H.nx*H.ny;

        // get cell-centered position
        Get_Position(i, j, k, &x_pos, &y_pos, &z_pos);

        if (x_pos < diaph)
        {
          C.density[id]    = rho_l;
          C.momentum_x[id] = rho_l * v_l;
          C.momentum_y[id] = 0.0;
          C.momentum_z[id] = 0.0;
          C.Energy[id]     = P_l/(gama-1.0) + 0.5*rho_l*v_l*v_l;
          #ifdef DE
          C.GasEnergy[id]  = P_l/(gama-1.0);
          #endif
        }
        else
        {
          C.density[id]    = rho_r;
          C.momentum_x[id] = rho_r * v_r;
          C.momentum_y[id] = 0.0;
          C.momentum_z[id] = 0.0;
          C.Energy[id]     = P_r/(gama-1.0) + 0.5*rho_r*v_r*v_r;        
          #ifdef DE
          C.GasEnergy[id]  = P_r/(gama-1.0);
          #endif
        }
      }
    }
  }
}


/*! \fn void Shu_Osher()
 *  \brief Initialize the grid with the Shu-Osher shock tube problem. See Stone 2008, Section 8.1 */
void Grid3D::Shu_Osher()
{
  int i, id;
  Real x_pos, y_pos, z_pos;
  Real vx, P;

  // set initial values of conserved variables
  for (i=H.n_ghost; i<H.nx-H.n_ghost; i++) {
    id = i;
    // get centered x position
    Get_Position(i, H.n_ghost, H.n_ghost, &x_pos, &y_pos, &z_pos);

    if (x_pos < -0.8)
    {
      C.density[id] = 3.857143;
      vx = 2.629369;
      C.momentum_x[id] = C.density[id]*vx;
      C.momentum_y[id] = 0.0;
      C.momentum_z[id] = 0.0;
      P = 10.33333;
      C.Energy[id] = P/(gama-1.0) + 0.5*C.density[id]*vx*vx;
    }
    else
    {
      C.density[id] = 1.0 + 0.2*sin(5.0*PI*x_pos);
      Real vx = 0.0;
      C.momentum_x[id] = C.density[id]*vx;
      C.momentum_y[id] = 0.0;
      C.momentum_z[id] = 0.0;
      Real P = 1.0;
      C.Energy[id] = P/(gama-1.0) + 0.5*C.density[id]*vx*vx;
    }
  }
}


/*! \fn void Blast_1D()
 *  \brief Initialize the grid with two interacting blast waves. See Stone 2008, Section 8.1.*/
void Grid3D::Blast_1D()
{
  int i, id;
  Real x_pos, y_pos, z_pos;
  Real vx, P;

  // set initial values of conserved variables
  for (i=H.n_ghost; i<H.nx-H.n_ghost; i++) {
    id = i;
    // get the centered x position
    Get_Position(i, H.n_ghost, H.n_ghost, &x_pos, &y_pos, &z_pos);

    if (x_pos < 0.1)
    {
      C.density[id] = 1.0;
      C.momentum_x[id] = 0.0;
      C.momentum_y[id] = 0.0;
      C.momentum_z[id] = 0.0;
      P = 1000.0;
      C.Energy[id] = P/(gama-1.0);
    }
    else if (x_pos > 0.9)
    {
      C.density[id] = 1.0;
      C.momentum_x[id] = 0.0;
      C.momentum_y[id] = 0.0;
      C.momentum_z[id] = 0.0;
      P = 100;
      C.Energy[id] = P/(gama-1.0);
    }        
    else
    {
      C.density[id] = 1.0;
      C.momentum_x[id] = 0.0;
      C.momentum_y[id] = 0.0;
      C.momentum_z[id] = 0.0;
      P = 0.01;
      C.Energy[id] = P/(gama-1.0);
    }
  }
}


/*! \fn void KH_discontinuous_2D()
 *  \brief Initialize the grid with a 2D Kelvin-Helmholtz instability. 
           This version of KH test has a discontinuous boundary.
           Use KH_res_ind_2D for a version that is resolution independent. */
void Grid3D::KH_discontinuous_2D()
{
  int i, j, id;
  Real x_pos, y_pos, z_pos;
  Real vx, vy, vz;


  // set the initial values of the conserved variables
  for (j=H.n_ghost; j<H.ny-H.n_ghost; j++) {
    for (i=H.n_ghost; i<H.nx-H.n_ghost; i++) {
      id = i + j*H.nx;
      // get the centered x and y positions
      Get_Position(i, j, H.n_ghost, &x_pos, &y_pos, &z_pos);

      // outer thirds of slab
      if (y_pos <= 1.0*H.ydglobal/3.0) 
      {
        C.density[id] = 1.0;
        C.momentum_x[id] = 0.5 + 0.01*sin(2*PI*x_pos);
        C.momentum_y[id] = 0.0 + 0.01*sin(2*PI*x_pos);
        C.momentum_z[id] = 0.0;
        C.Energy[id] = 2.5/(gama-1.0) + 0.5*(C.momentum_x[id]*C.momentum_x[id] + C.momentum_y[id]*C.momentum_y[id])/C.density[id];
      }
      else if (y_pos >= 2.0*H.ydglobal/3.0)
      {
        C.density[id] = 1.0;
        C.momentum_x[id] = 0.5 + 0.01*sin(2*PI*x_pos);
        C.momentum_y[id] = 0.0 + 0.01*sin(2*PI*x_pos);
        C.momentum_z[id] = 0.0;
        C.Energy[id] = 2.5/(gama-1.0) + 0.5*(C.momentum_x[id]*C.momentum_x[id] + C.momentum_y[id]*C.momentum_y[id])/C.density[id];
      }
      // inner third of slab
      else
      {
        C.density[id] = 2.0;
        C.momentum_x[id] = -1.0 + 0.02*sin(2*PI*x_pos);
        C.momentum_y[id] = 0.0  + 0.02*sin(2*PI*x_pos);
        C.momentum_z[id] = 0.0;
        C.Energy[id] = 2.5/(gama-1.0) + 0.5*(C.momentum_x[id]*C.momentum_x[id] + C.momentum_y[id]*C.momentum_y[id])/C.density[id];
      }
    }
  }

}


/*! \fn void KH_res_ind_2D()
 *  \brief Initialize the grid with a 2D Kelvin-Helmholtz instability whose modes are resolution independent. */
void Grid3D::KH_res_ind_2D()
{
  int i, j, id;
  Real x_pos, y_pos, z_pos;
  Real mx, my, mz;


  // set the initial values of the conserved variables
  for (j=H.n_ghost; j<H.ny-H.n_ghost; j++) {
    for (i=H.n_ghost; i<H.nx-H.n_ghost; i++) {
      id = i + j*H.nx;
      // get the centered x and y positions
      Get_Position(i, j, H.n_ghost, &x_pos, &y_pos, &z_pos);
      
      // inner half of slab
      if (fabs(y_pos-0.5) < 0.25)
      {
        if (y_pos > 0.5)
        {
          C.density[id] = 2.0 - exp( -0.5*pow(y_pos-0.75 - sqrt(-2.0*0.05*0.05*log(0.5)),2)/pow(0.05,2) );
          C.momentum_x[id] = 0.5*C.density[id] - C.density[id] * exp( -0.5*pow(y_pos-0.75 - sqrt(-2.0*0.05*0.05*log(0.5)),2) / pow(0.05,2) );
        }
        else
        {
          C.density[id] = 2.0 - exp( -0.5*pow(y_pos-0.25 + sqrt(-2.0*0.05*0.05*log(0.5)),2)/pow(0.05,2) );
          C.momentum_x[id] = 0.5*C.density[id] - C.density[id] * exp( -0.5*pow(y_pos-0.25 + sqrt(-2.0*0.05*0.05*log(0.5)),2) / pow(0.05,2) );
        }
        C.momentum_y[id] = 0.0 + C.density[id] * 0.1*sin(4*PI*x_pos);
        C.momentum_z[id] = 0.0;
        mx = C.momentum_x[id];
        my = C.momentum_y[id];
        mz = C.momentum_z[id];
        C.Energy[id] = 2.5/(gama-1.0) + 0.5*(mx*my + my*my + mz*mz)/C.density[id];
      }
      // outer quarters of slab
      else
      {
        if (y_pos > 0.5)
        {
          C.density[id] = 1.0 + exp( -0.5*pow(y_pos-0.75 + sqrt(-2.0*0.05*0.05*log(0.5)),2)/pow(0.05,2) );
          C.momentum_x[id] = -0.5*C.density[id] + C.density[id] * exp( -0.5*pow(y_pos-0.75 + sqrt(-2.0*0.05*0.05*log(0.5)),2)/pow(0.05,2) );
        }
        else
        {
          C.density[id] = 1.0 + exp( -0.5*pow(y_pos-0.25 - sqrt(-2.0*0.05*0.05*log(0.5)),2)/pow(0.05,2) );
          C.momentum_x[id] = -0.5*C.density[id] + C.density[id] * exp( -0.5*pow(y_pos-0.25 - sqrt(-2.0*0.05*0.05*log(0.5)),2)/pow(0.05,2) );
        }
        C.momentum_y[id] = 0.0 + C.density[id] * 0.1*sin(4*PI*x_pos);
        C.momentum_z[id] = 0.0;
        mx = C.momentum_x[id];
        my = C.momentum_y[id];
        mz = C.momentum_z[id];
        C.Energy[id] = 2.5/(gama-1.0) + 0.5*(mx*mx + my*my + mz*mz)/C.density[id];
      }
    }
  }


}



/*! \fn void Implosion_2D()
 *  \brief Implosion test described in Liska, 2003. */
void Grid3D::Implosion_2D()
{
  int i, j, id;
  Real x_pos, y_pos, z_pos;
  Real P;


  // set the initial values of the conserved variables
  for (j=H.n_ghost; j<H.ny-H.n_ghost; j++) {
    for (i=H.n_ghost; i<H.nx-H.n_ghost; i++) {
      id = i + j*H.nx;
      // get the centered x and y positions
      Get_Position(i, j, H.n_ghost, &x_pos, &y_pos, &z_pos);

      // inner corner of box
      if (y_pos < (0.1500001 - x_pos)) {
        C.density[id] = 0.125;
        C.momentum_x[id] = 0.0;
        C.momentum_y[id] = 0.0;
        C.momentum_z[id] = 0.0;
        P = 0.14;
        C.Energy[id] = P/(gama-1.0);
        #ifdef DE
        C.GasEnergy[id] = P/(gama-1.0);
        #endif
      }
      // everywhere else
      else {
        C.density[id] = 1.0;
        C.momentum_x[id] = 0.0;
        C.momentum_y[id] = 0.0;
        C.momentum_z[id] = 0.0;
        P = 1.0;
        C.Energy[id] = P/(gama-1.0);
        #ifdef DE
        C.GasEnergy[id] = P/(gama-1.0);
        #endif
      }
    }
  }

}


/*! \fn void Noh_2D()
 *  \brief Noh test described in Liska, 2003. */
void Grid3D::Noh_2D()
{
  int i, j, id;
  Real x_pos, y_pos, z_pos;
  Real vx, vy, P, r;


  // set the initial values of the conserved variables
  for (j=H.n_ghost; j<H.ny-H.n_ghost; j++) {
    for (i=H.n_ghost; i<H.nx-H.n_ghost; i++) {
      id = i + j*H.nx;
      // get the centered x and y positions at (x,y,z)
      Get_Position(i, j, H.n_ghost, &x_pos, &y_pos, &z_pos);

      C.density[id] = 1.0;
      r = sqrt(x_pos*x_pos + y_pos*y_pos);
      vx = x_pos / r;
      vy = y_pos / r;
      C.momentum_x[id] = - x_pos / r;
      C.momentum_y[id] = - y_pos / r;
      C.momentum_z[id] = 0.0;
      C.Energy[id] = 1.0e-6/(gama-1.0) + 0.5;
    }
  }

}



/*! \fn void Noh_3D()
 *  \brief Noh test described in Stone, 2008. */
void Grid3D::Noh_3D()
{
  int i, j, k, id;
  Real x_pos, y_pos, z_pos, r;


  // set the initial values of the conserved variables
  for (k=H.n_ghost; k<H.nz-H.n_ghost; k++) {
    for (j=H.n_ghost; j<H.ny-H.n_ghost; j++) {
      for (i=H.n_ghost; i<H.nx-H.n_ghost; i++) {
        id = i + j*H.nx + k*H.nx*H.ny;

        // get the centered cell positions at (i,j,k)
        Get_Position(i, j, k, &x_pos, &y_pos, &z_pos);

        C.density[id] = 1.0;
        r = sqrt(x_pos*x_pos + y_pos*y_pos + z_pos*z_pos);
        C.momentum_x[id] = - x_pos / r;
        C.momentum_y[id] = - y_pos / r;
        C.momentum_z[id] = - z_pos / r;
        C.Energy[id] = 1.0e-6/(gama-1.0) + 0.5;
      }
    }
  }



}


