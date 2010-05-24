/****************************************************************************
* Copyright (C) 2009      Daniel Chappuis                                  *
****************************************************************************
* This file is part of ReactPhysics3D.                                     *
*                                                                          *
* ReactPhysics3D is free software: you can redistribute it and/or modify   *
* it under the terms of the GNU Lesser General Public License as published *
* by the Free Software Foundation, either version 3 of the License, or     *
* (at your option) any later version.                                      *
*                                                                          *
* ReactPhysics3D is distributed in the hope that it will be useful,        *
* but WITHOUT ANY WARRANTY; without even the implied warranty of           *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the             *
* GNU Lesser General Public License for more details.                      *
*                                                                          *
* You should have received a copy of the GNU Lesser General Public License *
* along with ReactPhysics3D. If not, see <http://www.gnu.org/licenses/>.   *
***************************************************************************/

// Libraries
#include "LCPProjectedGaussSeidel.h"
#include <cmath>

using namespace reactphysics3d;

// Constructor
LCPProjectedGaussSeidel::LCPProjectedGaussSeidel(uint maxIterations)
                        :LCPSolver(maxIterations) {

}

// Destructor
LCPProjectedGaussSeidel::~LCPProjectedGaussSeidel() {

}

// Solve a LCP problem using the Projected-Gauss-Seidel algorithm
// This method outputs the result in the lambda vector
void LCPProjectedGaussSeidel::solve(const Matrix** const J_sp, const Matrix** const B_sp, uint nbConstraints,
                                    uint nbBodies, const Body*** const bodyMapping, std::map<Body*, uint> bodyNumberMapping,
                                    const Vector& b, const Vector& lowLimits, const Vector& highLimits, Vector& lambda) const {
    lambda = lambdaInit;
    double d[] = new double[nbConstraints];         // TODO : Avoid those kind of memory allocation here for optimization (allocate once in the object)
    Body* indexBody1, indexBody2;
    double deltaLambda;
    double lambdaTemp;
    uint i, iter;
    Vector* a = new Vector(6)[nbBodies];           // Array that contains nbBodies vector of dimension 6x1

    // For each constraint
    for (i=0; i<nbConstraints; i++) {
        d[i] = (J_sp[i][0] * B_sp[0][i] + J_sp[i][1] * B_sp[1][i]).getValue(0,0);
    }

    for(iter=0; iter<maxIterations; iter++) {
        for (i=0; i<nbConstraints; i++) {
            indexBody1 = bodyNumberMapping[bodyMapping[i][0]];
            indexBody2 = bodyNumberMapping[bodyMapping[i][1]];
            deltaLambda = (b(i) - J_sp[i][0]*a[indexBody1] - J_sp[i][1]*a[indexBody2]) / d[i];
            lambdaTemp = lambda.getValue(i);
            lambda.setValue(i, std::max(lowLimits.getValue(i), std::min(lambda.getValue(i) + deltaLambda, highLimits.getValue(i))));
            deltaLambda = lambda.getValue(i) - lambdaTemp;
            a[indexBody1] = a[indexBody1] + deltaLambda * B_sp[0][i];
            a[indexBody2] = a[indexBody2] + deltaLambda * B_sp[1][i];
        }
    }

    // Clean
    delete[] d;
    delete[] a;
}

// Compute the vector a used in the solve() method
// Note that a = B * lambda
void LCPProjectedGaussSeidel::computeVectorA(const Vector& lambda, uint nbConstraints, const Body*** const bodyMapping,
                                             const Matrix** const B_sp, std::map<Body*, uint> bodyNumberMapping,
                                             Vector* const a, uint nbBodies) const {
    uint i;
    Body* indexBody1, indexBody2;
    
    // Init the vector a with zero values
    for (i=0; i<nbBodies; i++) {
       a[i].initWithValue(0.0);
    }

    for(i=0; i<nbConstraints; i++) {
        indexBody1 = bodyNumberMapping[bodyMapping[i][0]];
        indexBody2 = bodyNumberMapping[bodyMapping[i][1]];
        a[indexBody1] = a[indexBody1] + B_sp[0][i] * lambda(i);
        a[indexBody2] = a[indexBody2] + B_sp[1][i] * lambda(i);
    }

}
