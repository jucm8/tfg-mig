/*
    -- MAGMA (version 2.7.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date November 2022

       @generated from testing/testing_zgemm.cpp, normal z -> d, Thu Nov 10 21:02:30 2022
       @author Mark Gates
*/
// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// includes, project
#include "flops.h"
#include "magma_v2.h"
#include "magma_lapack.h"
#include "magma_operators.h"
#include "testings.h"

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing dgemm
*/
int main( int argc, char** argv)
{
    #ifdef MAGMA_HAVE_OPENCL
    #define dA(i_, j_)  dA, ((i_) + (j_)*ldda)
    #define dB(i_, j_)  dB, ((i_) + (j_)*lddb)
    #define dC(i_, j_)  dC, ((i_) + (j_)*lddc)
    #else
    #define dA(i_, j_) (dA + (i_) + (j_)*ldda)
    #define dB(i_, j_) (dB + (i_) + (j_)*lddb)
    #define dC(i_, j_) (dC + (i_) + (j_)*lddc)
    #endif

    TESTING_CHECK( magma_init() );
    magma_print_environment();

    real_Double_t   gflops, magma_perf, magma_time, dev_perf, dev_time, cpu_perf, cpu_time;
    double          magma_error, dev_error, work[1];
    magma_int_t M, N, K;
    magma_int_t Am, An, Bm, Bn;
    magma_int_t sizeA, sizeB, sizeC;
    magma_int_t lda, ldb, ldc, ldda, lddb, lddc;
    magma_int_t ione     = 1;
    magma_int_t ISEED[4] = {0,0,0,1};
    int status = 0;

    double *hA, *hB, *hC, *hCmagma, *hCdev;
    magmaDouble_ptr dA, dB, dC;
    double c_neg_one = MAGMA_D_NEG_ONE;
    double alpha = MAGMA_D_MAKE(  0.29, -0.86 );
    double beta  = MAGMA_D_MAKE( -0.48,  0.38 );

    // used only with CUDA
    MAGMA_UNUSED( magma_perf );
    MAGMA_UNUSED( magma_time );
    MAGMA_UNUSED( magma_error );

    magma_opts opts;
    opts.parse_opts( argc, argv );

    // Allow 3*eps; real needs 2*sqrt(2) factor; see Higham, 2002, sec. 3.6.
    double eps = lapackf77_dlamch("E");
    double tol = 3*eps;

    #if defined(MAGMA_HAVE_CUDA) || defined(MAGMA_HAVE_HIP)
        // for CUDA/HIP, we can check MAGMA vs. CUBLAS/hipBLAS, without running LAPACK
        printf("%% If running lapack (option --lapack), MAGMA and %s error are both computed\n"
               "%% relative to CPU BLAS result. Else, MAGMA error is computed relative to %s result.\n\n",
                g_platform_str, g_platform_str );
        printf("%% transA = %s, transB = %s\n",
               lapack_trans_const(opts.transA),
               lapack_trans_const(opts.transB) );
        printf("%%   M     N     K   MAGMA Gflop/s (ms)  %s Gflop/s (ms)   CPU Gflop/s (ms)  MAGMA error  %s error\n",
                g_platform_str, g_platform_str );
    #else
        // for others, we need LAPACK for check
        opts.lapack |= opts.check;  // check (-c) implies lapack (-l)
        printf("%% transA = %s, transB = %s\n",
               lapack_trans_const(opts.transA),
               lapack_trans_const(opts.transB) );
        printf("%%   M     N     K   %s Gflop/s (ms)   CPU Gflop/s (ms)  %s error\n",
                g_platform_str, g_platform_str );
    #endif
    printf("%%========================================================================================================\n");
    for( int itest = 0; itest < opts.ntest; ++itest ) {
        
        M = opts.msize[itest];
        N = opts.nsize[itest];
        K = opts.ksize[itest];
        gflops = FLOPS_DGEMM( M, N, K ) / 1e9;

        if ( opts.transA == MagmaNoTrans ) {
            lda = Am = M;
            An = K;
        } else {
            lda = Am = K;
            An = M;
        }

        if ( opts.transB == MagmaNoTrans ) {
            ldb = Bm = K;
            Bn = N;
        } else {
            ldb = Bm = N;
            Bn = K;
        }
        ldc = M;

        ldda = magma_roundup( lda, opts.align );  // multiple of 32 by default
        lddb = magma_roundup( ldb, opts.align );  // multiple of 32 by default
        lddc = magma_roundup( ldc, opts.align );  // multiple of 32 by default

        sizeA = lda*An;
        sizeB = ldb*Bn;
        sizeC = ldc*N;

        TESTING_CHECK( magma_dmalloc_cpu( &hA,       lda*An ));
        TESTING_CHECK( magma_dmalloc_cpu( &hB,       ldb*Bn ));
        TESTING_CHECK( magma_dmalloc_cpu( &hC,       ldc*N  ));
        TESTING_CHECK( magma_dmalloc_cpu( &hCmagma,  ldc*N  ));
        TESTING_CHECK( magma_dmalloc_cpu( &hCdev,    ldc*N  ));

        TESTING_CHECK( magma_dmalloc( &dA, ldda*An ));
        TESTING_CHECK( magma_dmalloc( &dB, lddb*Bn ));
        TESTING_CHECK( magma_dmalloc( &dC, lddc*N  ));

        /* Initialize the matrices */
        lapackf77_dlarnv( &ione, ISEED, &sizeA, hA );
        lapackf77_dlarnv( &ione, ISEED, &sizeB, hB );
        lapackf77_dlarnv( &ione, ISEED, &sizeC, hC );

        magma_dsetmatrix( Am, An, hA, lda, dA(0,0), ldda, opts.queue );
        magma_dsetmatrix( Bm, Bn, hB, ldb, dB(0,0), lddb, opts.queue );

        // for error checks
        double Anorm = lapackf77_dlange( "F", &Am, &An, hA, &lda, work );
        double Bnorm = lapackf77_dlange( "F", &Bm, &Bn, hB, &ldb, work );
        double Cnorm = lapackf77_dlange( "F", &M,  &N,  hC, &ldc, work );


        magma_dsetmatrix( M, N, hC, ldc, dC(0,0), lddc, opts.queue );
        magma_flush_cache( opts.cache );

        magma_time = 0.0;
        magma_perf = 0.0;
        cpu_perf = 0.0;
        cpu_time = 0.0;

        real_Double_t t0 = magma_sync_wtime( opts.queue );
        real_Double_t t1 = t0;
        magma_int_t niter = 0;
        while (t1 - t0 < opts.seconds) {

            magma_dgemm( opts.transA, opts.transB, M, N, K,
                         alpha, dA(0,0), ldda,
                                dB(0,0), lddb,
                         beta,  dC(0,0), lddc, opts.queue );

            t1 = magma_sync_wtime( opts.queue );
            niter++;
        }
        real_Double_t elapsed = t1 - t0;
        dev_perf = gflops * niter / elapsed;

        magma_dgetmatrix( M, N, dC(0,0), lddc, hCdev, ldc, opts.queue );

                /* =====================================================================
            Check the result
            =================================================================== */
        if ( opts.lapack ) {
            // Compute forward error bound (see Higham, 2002, sec. 3.5),
            // modified to include alpha, beta, and input C.
            // ||R_magma - R_ref||_p / (gamma_{K+2} |alpha| ||A||_p ||B||_p + 2 |beta| ||C||_p ) < eps/2.
            // This should work with p = 1, inf, fro, but numerical tests
            // show p = 1, inf are very spiky and sometimes exceed eps.
            // We use gamma_n = sqrt(n)*u instead of n*u/(1-n*u), since the
            // former accurately represents statistical average rounding.
            // We allow a slightly looser tolerance.

            // use LAPACK for R_ref
            blasf77_daxpy( &sizeC, &c_neg_one, hC, &ione, hCdev, &ione );
            dev_error = lapackf77_dlange( "F", &M, &N, hCdev, &ldc, work )
                        / (sqrt(double(K+2))*fabs(alpha)*Anorm*Bnorm + 2*fabs(beta)*Cnorm);

            #if defined(MAGMA_HAVE_CUDA) || defined(MAGMA_HAVE_HIP)
                blasf77_daxpy( &sizeC, &c_neg_one, hC, &ione, hCmagma, &ione );
                magma_error = lapackf77_dlange( "F", &M, &N, hCmagma, &ldc, work )
                        / (sqrt(double(K+2))*fabs(alpha)*Anorm*Bnorm + 2*fabs(beta)*Cnorm);

                bool okay = (magma_error < tol && dev_error < tol);
                status += ! okay;
                printf("%5lld %5lld %5lld   %7.2f (%7.2f)    %7.2f (%7.2f)   %7.2f (%7.2f)    %8.2e     %8.2e\n",
                        (long long) M, (long long) N, (long long) K,
                        magma_perf,  1000.*magma_time,
                        dev_perf,    1000.*elapsed,
                        cpu_perf,    1000.*cpu_time,
                        magma_error, dev_error);
            #else
                bool okay = (dev_error < tol);
                status += ! okay;
                printf("%5lld %5lld %5lld   %7.2f (%7.2f)   %7.2f (%7.2f)    %8.2e\n",
                        (long long) M, (long long) N, (long long) K,
                        dev_perf,    1000.*elapsed,
                        cpu_perf,    1000.*cpu_time,
                        dev_error);
            #endif
        }
        else {
            #if defined(MAGMA_HAVE_CUDA) || defined(MAGMA_HAVE_HIP)

                // use cuBLAS for R_ref (currently only with CUDA)
                blasf77_daxpy( &sizeC, &c_neg_one, hCdev, &ione, hCmagma, &ione );
                magma_error = lapackf77_dlange( "F", &M, &N, hCmagma, &ldc, work )
                        / (sqrt(double(K+2))*fabs(alpha)*Anorm*Bnorm + 2*fabs(beta)*Cnorm);

                bool okay = (magma_error < tol);
                status += ! okay;
                printf("%5lld %5lld %5lld   %7.2f (%7.2f)    %7.2f (%7.2f)     ---   (  ---  )    %8.2e        ---\n",
                        (long long) M, (long long) N, (long long) K,
                        magma_perf,  1000.*magma_time,
                        dev_perf,    1000.*elapsed,
                        magma_error,
                        (okay ? "ok" : "failed"));
            #else
                printf("%5lld %5lld %5lld   %7.2f (%7.2f)     ---   (  ---  )       ---\n",
                        (long long) M, (long long) N, (long long) K,
                        dev_perf,    1000.*elapsed );
            #endif
        }



        magma_free_cpu( hA );
        magma_free_cpu( hB );
        magma_free_cpu( hC );
        magma_free_cpu( hCmagma  );
        magma_free_cpu( hCdev    );

        magma_free( dA );
        magma_free( dB );
        magma_free( dC );
        fflush( stdout );
        // if ( opts.niter > 1 ) {
        //     printf( "\n" );
        // }
    }

    opts.cleanup();
    TESTING_CHECK( magma_finalize() );
    return status;
}