/*! \file VL_3D_cuda.cu
 *  \brief Definitions of the cuda 3D VL algorithm functions. */

#ifdef CUDA
#ifdef VL

#include<stdio.h>
#include<stdlib.h>
#include<math.h>
#include<cuda.h>
#include"global.h"
#include"global_cuda.h"
#include"hydro_cuda.h"
#include"VL_3D_cuda.h"
#include"pcm_cuda.h"
#include"plmp_vl_cuda.h"
#include"plmc_vl_cuda.h"
#include"ppmp_vl_cuda.h"
#include"ppmc_vl_cuda.h"
#include"exact_cuda.h"
#include"roe_cuda.h"
#include"h_correction_3D_cuda.h"
#include"cooling_cuda.h"
#include"subgrid_routines_3D.h"


__global__ void Update_Conserved_Variables_3D_half(Real *dev_conserved, Real *dev_conserved_half, Real *dev_F_x, Real *dev_F_y,  Real *dev_F_z,
                                              int nx, int ny, int nz, int n_ghost, Real dx, Real dy, Real dz, Real dt, Real gamma);



Real VL_Algorithm_3D_CUDA(Real *host_conserved, int nx, int ny, int nz, int n_ghost, Real dx, Real dy, Real dz, Real dt)
{

  //Here, *host_conserved contains the entire
  //set of conserved variables on the grid
  //concatenated into a 1-d array

  int n_fields = 5;
  #ifdef DE
  n_fields++;
  #endif

  // dimensions of subgrid blocks
  int nx_s; //number of cells in the subgrid block along x direction
  int ny_s; //number of cells in the subgrid block along y direction
  int nz_s; //number of cells in the subgrid block along z direction

  // total number of blocks needed
  int block_tot;    //total number of subgrid blocks (unsplit == 1)
  int block1_tot;   //total number of subgrid blocks in x direction
  int block2_tot;   //total number of subgrid blocks in y direction
  int block3_tot;   //total number of subgrid blocks in z direction 
  int remainder1;   //modulus of number of cells after block subdivision in x direction
  int remainder2;   //modulus of number of cells after block subdivision in y direction 
  int remainder3;   //modulus of number of cells after block subdivision in z direction

  // counter for which block we're on
  int block = 0;

  // calculate the dimensions for each subgrid block
  sub_dimensions_3D(nx, ny, nz, n_ghost, &nx_s, &ny_s, &nz_s, &block1_tot, &block2_tot, &block3_tot, &remainder1, &remainder2, &remainder3, n_fields);
  block_tot = block1_tot*block2_tot*block3_tot;

  // number of cells in one subgrid block
  int BLOCK_VOL = nx_s*ny_s*nz_s;


  // define the dimensions for the 1D grid
  int  ngrid = (BLOCK_VOL + TPB - 1) / TPB;

  //number of blocks per 1-d grid  
  dim3 dim1dGrid(ngrid, 1, 1);

  //number of threads per 1-d block   
  dim3 dim1dBlock(TPB, 1, 1);


  // allocate buffer arrays to copy conserved variable slices into
  Real **buffer;
  allocate_buffers_3D(block1_tot, block2_tot, block3_tot, BLOCK_VOL, buffer, n_fields);
  // and set up pointers for the location to copy from and to
  Real *tmp1;
  Real *tmp2;


  // allocate an array on the CPU to hold max_dti returned from each thread block
  Real max_dti = 0;
  Real *host_dti_array;
  host_dti_array = (Real *) malloc(ngrid*sizeof(Real));

  // allocate GPU arrays
  // conserved variables
  Real *dev_conserved, *dev_conserved_half;
  // input states and associated interface fluxes (Q* and F* from Stone, 2008)
  Real *Q_Lx, *Q_Rx, *Q_Ly, *Q_Ry, *Q_Lz, *Q_Rz, *F_x, *F_y, *F_z;
  // arrays to hold the eta values for the H correction
  Real *eta_x, *eta_y, *eta_z, *etah_x, *etah_y, *etah_z;
  // array of inverse timesteps for dt calculation
  Real *dev_dti_array;

  // allocate memory on the GPU
  CudaSafeCall( cudaMalloc((void**)&dev_conserved, n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&dev_conserved_half, n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&Q_Lx,  n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&Q_Rx,  n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&Q_Ly,  n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&Q_Ry,  n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&Q_Lz,  n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&Q_Rz,  n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&F_x,   n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&F_y,   n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&F_z,   n_fields*BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&eta_x,  BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&eta_y,  BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&eta_z,  BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&etah_x, BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&etah_y, BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&etah_z, BLOCK_VOL*sizeof(Real)) );
  CudaSafeCall( cudaMalloc((void**)&dev_dti_array, ngrid*sizeof(Real)) );


  // transfer first conserved variable slice into the first buffer
  host_copy_init_3D(nx, ny, nz, nx_s, ny_s, nz_s, n_ghost, block, block1_tot, block2_tot, remainder1, remainder2, BLOCK_VOL, host_conserved, buffer, &tmp1, &tmp2, n_fields);


  // START LOOP OVER SUBGRID BLOCKS HERE
  while (block < block_tot) {

  // zero the GPU arrays
  cudaMemset(dev_conserved, 0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(dev_conserved_half, 0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(Q_Lx,  0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(Q_Rx,  0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(Q_Ly,  0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(Q_Ry,  0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(Q_Lz,  0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(Q_Rz,  0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(F_x,   0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(F_y,   0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(F_z,   0, n_fields*BLOCK_VOL*sizeof(Real));
  cudaMemset(eta_x,  0, BLOCK_VOL*sizeof(Real));
  cudaMemset(eta_y,  0, BLOCK_VOL*sizeof(Real));
  cudaMemset(eta_z,  0, BLOCK_VOL*sizeof(Real));
  cudaMemset(etah_x, 0, BLOCK_VOL*sizeof(Real));
  cudaMemset(etah_y, 0, BLOCK_VOL*sizeof(Real));
  cudaMemset(etah_z, 0, BLOCK_VOL*sizeof(Real));
  cudaMemset(dev_dti_array, 0, ngrid*sizeof(Real));  
  CudaCheckError();


  // copy the conserved variables onto the GPU
  CudaSafeCall( cudaMemcpy(dev_conserved, tmp1, n_fields*BLOCK_VOL*sizeof(Real), cudaMemcpyHostToDevice) );
  

  // Step 1: Use PCM reconstruction to put primitive variables into interface arrays
  PCM_Reconstruction_3D<<<dim1dGrid,dim1dBlock>>>(dev_conserved, Q_Lx, Q_Rx, Q_Ly, Q_Ry, Q_Lz, Q_Rz, nx_s, ny_s, nz_s, n_ghost, gama);
  CudaCheckError();


  // Step 2: Calculate first-order upwind fluxes 
  #ifdef EXACT
  Calculate_Exact_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Lx, Q_Rx, F_x, nx_s, ny_s, nz_s, n_ghost, gama, 0);
  Calculate_Exact_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Ly, Q_Ry, F_y, nx_s, ny_s, nz_s, n_ghost, gama, 1);
  Calculate_Exact_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Lz, Q_Rz, F_z, nx_s, ny_s, nz_s, n_ghost, gama, 2);
  #endif //EXACT
  #ifdef ROE
  Calculate_Roe_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Lx, Q_Rx, F_x, nx_s, ny_s, nz_s, n_ghost, gama, etah_x, 0);
  Calculate_Roe_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Ly, Q_Ry, F_y, nx_s, ny_s, nz_s, n_ghost, gama, etah_y, 1);
  Calculate_Roe_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Lz, Q_Rz, F_z, nx_s, ny_s, nz_s, n_ghost, gama, etah_z, 2);
  #endif //ROE
  CudaCheckError();


  // Step 3: Update the conserved variables half a timestep 
  Update_Conserved_Variables_3D_half<<<dim1dGrid,dim1dBlock>>>(dev_conserved, dev_conserved_half, F_x, F_y, F_z, nx_s, ny_s, nz_s, n_ghost, dx, dy, dz, 0.5*dt, gama);
  CudaCheckError();


  // Step 4: Construct left and right interface values using updated conserved variables
  #ifdef PCM
  PCM_Reconstruction_3D<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Lx, Q_Rx, Q_Ly, Q_Ry, Q_Lz, Q_Rz, nx_s, ny_s, nz_s, n_ghost, gama);
  #endif
  #ifdef PLMP
  PLMP_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Lx, Q_Rx, nx_s, ny_s, nz_s, n_ghost, gama, 0);
  PLMP_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Ly, Q_Ry, nx_s, ny_s, nz_s, n_ghost, gama, 1);
  PLMP_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Lz, Q_Rz, nx_s, ny_s, nz_s, n_ghost, gama, 2);
  #endif //PLMP 
  #ifdef PLMC
  PLMC_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Lx, Q_Rx, nx_s, ny_s, nz_s, n_ghost, gama, 0);
  PLMC_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Ly, Q_Ry, nx_s, ny_s, nz_s, n_ghost, gama, 1);
  PLMC_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Lz, Q_Rz, nx_s, ny_s, nz_s, n_ghost, gama, 2);  
  #endif
  #ifdef PPMP
  PPMP_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Lx, Q_Rx, nx_s, ny_s, nz_s, n_ghost, gama, 0);
  PPMP_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Ly, Q_Ry, nx_s, ny_s, nz_s, n_ghost, gama, 1);
  PPMP_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Lz, Q_Rz, nx_s, ny_s, nz_s, n_ghost, gama, 2);
  #endif //PPMP
  #ifdef PPMC
  PPMC_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Lx, Q_Rx, nx_s, ny_s, nz_s, n_ghost, gama, 0);
  PPMC_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Ly, Q_Ry, nx_s, ny_s, nz_s, n_ghost, gama, 1);
  PPMC_VL<<<dim1dGrid,dim1dBlock>>>(dev_conserved_half, Q_Lz, Q_Rz, nx_s, ny_s, nz_s, n_ghost, gama, 2);
  #endif //PPMC
  CudaCheckError();
  

  #ifdef H_CORRECTION
  // Step 4.5: Calculate eta values for H correction
  calc_eta_x_3D<<<dim1dGrid,dim1dBlock>>>(Q_Lx, Q_Rx, eta_x, nx_s, ny_s, nz_s, n_ghost, gama);
  calc_eta_y_3D<<<dim1dGrid,dim1dBlock>>>(Q_Ly, Q_Ry, eta_y, nx_s, ny_s, nz_s, n_ghost, gama);
  calc_eta_z_3D<<<dim1dGrid,dim1dBlock>>>(Q_Lz, Q_Rz, eta_z, nx_s, ny_s, nz_s, n_ghost, gama);
  CudaCheckError();
  // and etah values for each interface
  calc_etah_x_3D<<<dim1dGrid,dim1dBlock>>>(eta_x, eta_y, eta_z, etah_x, nx_s, ny_s, nz_s, n_ghost);
  calc_etah_y_3D<<<dim1dGrid,dim1dBlock>>>(eta_x, eta_y, eta_z, etah_y, nx_s, ny_s, nz_s, n_ghost);
  calc_etah_z_3D<<<dim1dGrid,dim1dBlock>>>(eta_x, eta_y, eta_z, etah_z, nx_s, ny_s, nz_s, n_ghost);
  CudaCheckError();
  #endif //H_CORRECTION


  // Step 5: Calculate the fluxes again
  #ifdef EXACT
  Calculate_Exact_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Lx, Q_Rx, F_x, nx_s, ny_s, nz_s, n_ghost, gama, 0);
  Calculate_Exact_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Ly, Q_Ry, F_y, nx_s, ny_s, nz_s, n_ghost, gama, 1);
  Calculate_Exact_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Lz, Q_Rz, F_z, nx_s, ny_s, nz_s, n_ghost, gama, 2);
  #endif //EXACT
  #ifdef ROE
  Calculate_Roe_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Lx, Q_Rx, F_x, nx_s, ny_s, nz_s, n_ghost, gama, etah_x, 0);
  Calculate_Roe_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Ly, Q_Ry, F_y, nx_s, ny_s, nz_s, n_ghost, gama, etah_y, 1);
  Calculate_Roe_Fluxes<<<dim1dGrid,dim1dBlock>>>(Q_Lz, Q_Rz, F_z, nx_s, ny_s, nz_s, n_ghost, gama, etah_z, 2);
  #endif //ROE
  CudaCheckError();


  // Step 6: Update the conserved variable array
  Update_Conserved_Variables_3D<<<dim1dGrid,dim1dBlock>>>(dev_conserved, F_x, F_y, F_z, nx_s, ny_s, nz_s, n_ghost, dx, dy, dz, dt, gama);
  CudaCheckError();

  #ifdef DE
  Sync_Energies_3D<<<dim1dGrid,dim1dBlock>>>(dev_conserved, nx_s, ny_s, nz_s, n_ghost, gama);
  #endif

  // Apply cooling
  #ifdef COOLING_GPU
  cooling_kernel<<<dim1dGrid,dim1dBlock>>>(dev_conserved, nx_s, ny_s, nz_s, n_ghost, dt, gama);
  #endif

  
  // Step 7: Calculate the next time step
  Calc_dt_3D<<<dim1dGrid,dim1dBlock>>>(dev_conserved, nx_s, ny_s, nz_s, n_ghost, dx, dy, dz, dev_dti_array, gama);
  CudaCheckError();

  // copy the updated conserved variable array back to the CPU
  CudaSafeCall( cudaMemcpy(tmp2, dev_conserved, n_fields*BLOCK_VOL*sizeof(Real), cudaMemcpyDeviceToHost) );

  // copy the next conserved variable blocks into appropriate buffers
  host_copy_next_3D(nx, ny, nz, nx_s, ny_s, nz_s, n_ghost, block, block1_tot, block2_tot, block3_tot, remainder1, remainder2, remainder3, BLOCK_VOL, host_conserved, buffer, &tmp1, n_fields);

  // copy the updated conserved variable array back into the host_conserved array on the CPU
  host_return_values_3D(nx, ny, nz, nx_s, ny_s, nz_s, n_ghost, block, block1_tot, block2_tot, block3_tot, remainder1, remainder2, remainder3, BLOCK_VOL, host_conserved, buffer, n_fields);


  // copy the dti array onto the CPU
  CudaSafeCall( cudaMemcpy(host_dti_array, dev_dti_array, ngrid*sizeof(Real), cudaMemcpyDeviceToHost) );
  // iterate through to find the maximum inverse dt for this subgrid block
  for (int i=0; i<ngrid; i++) {
    max_dti = fmax(max_dti, host_dti_array[i]);
  }


  // add one to the counter
  block++;

}


  // free CPU memory
  free(host_dti_array);  
  free_buffers_3D(nx, ny, nz, nx_s, ny_s, nz_s, block1_tot, block2_tot, block3_tot, buffer);


  // free the GPU memory
  cudaFree(dev_conserved);
  cudaFree(dev_conserved_half);
  cudaFree(Q_Lx);
  cudaFree(Q_Rx);
  cudaFree(Q_Ly);
  cudaFree(Q_Ry);
  cudaFree(Q_Lz);
  cudaFree(Q_Rz);
  cudaFree(F_x);
  cudaFree(F_y);
  cudaFree(F_z);
  cudaFree(eta_x);
  cudaFree(eta_y);
  cudaFree(eta_z);
  cudaFree(etah_x);
  cudaFree(etah_y);
  cudaFree(etah_z);
  cudaFree(dev_dti_array);


  // return the maximum inverse timestep
  return max_dti;

}



__global__ void Update_Conserved_Variables_3D_half(Real *dev_conserved, Real *dev_conserved_half, Real *dev_F_x, Real *dev_F_y,  Real *dev_F_z,
                                              int nx, int ny, int nz, int n_ghost, Real dx, Real dy, Real dz, Real dt, Real gamma)
{

  int id, xid, yid, zid, n_cells;
  int imo, jmo, kmo;

  Real dtodx = dt/dx;
  Real dtody = dt/dy;
  Real dtodz = dt/dz;
  n_cells = nx*ny*nz;

  // get a global thread ID
  id = threadIdx.x + blockIdx.x * blockDim.x;
  zid = id / (nx*ny);
  yid = (id - zid*nx*ny) / nx;
  xid = id - zid*nx*ny - yid*nx;


  // threads corresponding to all cells except outer ring of ghost cells do the calculation
  if (xid > 0 && xid < nx-1 && yid > 0 && yid < ny-1 && zid > 0 && zid < nz-1)
  {
    // update the conserved variable array
    imo = xid-1 + yid*nx + zid*nx*ny;
    jmo = xid + (yid-1)*nx + zid*nx*ny;
    kmo = xid + yid*nx + (zid-1)*nx*ny;
    dev_conserved_half[            id] = dev_conserved[            id]
                                       + dtodx * (dev_F_x[            imo] - dev_F_x[            id])
                                       + dtody * (dev_F_y[            jmo] - dev_F_y[            id])
                                       + dtodz * (dev_F_z[            kmo] - dev_F_z[            id]);
    dev_conserved_half[  n_cells + id] = dev_conserved[  n_cells + id] 
                                       + dtodx * (dev_F_x[  n_cells + imo] - dev_F_x[  n_cells + id])
                                       + dtody * (dev_F_y[  n_cells + jmo] - dev_F_y[  n_cells + id])
                                       + dtodz * (dev_F_z[  n_cells + kmo] - dev_F_z[  n_cells + id]);
    dev_conserved_half[2*n_cells + id] = dev_conserved[2*n_cells + id] 
                                       + dtodx * (dev_F_x[2*n_cells + imo] - dev_F_x[2*n_cells + id])
                                       + dtody * (dev_F_y[2*n_cells + jmo] - dev_F_y[2*n_cells + id])
                                       + dtodz * (dev_F_z[2*n_cells + kmo] - dev_F_z[2*n_cells + id]);
    dev_conserved_half[3*n_cells + id] = dev_conserved[3*n_cells + id] 
                                       + dtodx * (dev_F_x[3*n_cells + imo] - dev_F_x[3*n_cells + id])
                                       + dtody * (dev_F_y[3*n_cells + jmo] - dev_F_y[3*n_cells + id])
                                       + dtodz * (dev_F_z[3*n_cells + kmo] - dev_F_z[3*n_cells + id]);
    dev_conserved_half[4*n_cells + id] = dev_conserved[4*n_cells + id] 
                                       + dtodx * (dev_F_x[4*n_cells + imo] - dev_F_x[4*n_cells + id])
                                       + dtody * (dev_F_y[4*n_cells + jmo] - dev_F_y[4*n_cells + id])
                                       + dtodz * (dev_F_z[4*n_cells + kmo] - dev_F_z[4*n_cells + id]);
    if (dev_conserved_half[id] < 0.0 || dev_conserved_half[id] != dev_conserved_half[id]) {
      printf("%3d %3d %3d Thread crashed in half step update.\n", xid, yid, zid);
    }    


  }

}


#endif //VL
#endif //CUDA
