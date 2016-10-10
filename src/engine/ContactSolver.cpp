/********************************************************************************
* ReactPhysics3D physics library, http://www.reactphysics3d.com                 *
* Copyright (c) 2010-2016 Daniel Chappuis                                       *
*********************************************************************************
*                                                                               *
* This software is provided 'as-is', without any express or implied warranty.   *
* In no event will the authors be held liable for any damages arising from the  *
* use of this software.                                                         *
*                                                                               *
* Permission is granted to anyone to use this software for any purpose,         *
* including commercial applications, and to alter it and redistribute it        *
* freely, subject to the following restrictions:                                *
*                                                                               *
* 1. The origin of this software must not be misrepresented; you must not claim *
*    that you wrote the original software. If you use this software in a        *
*    product, an acknowledgment in the product documentation would be           *
*    appreciated but is not required.                                           *
*                                                                               *
* 2. Altered source versions must be plainly marked as such, and must not be    *
*    misrepresented as being the original software.                             *
*                                                                               *
* 3. This notice may not be removed or altered from any source distribution.    *
*                                                                               *
********************************************************************************/

// Libraries
#include "ContactSolver.h"
#include "DynamicsWorld.h"
#include "body/RigidBody.h"
#include "Profiler.h"
#include <limits>

using namespace reactphysics3d;
using namespace std;

// Constants initialization
const decimal ContactSolver::BETA = decimal(0.2);
const decimal ContactSolver::BETA_SPLIT_IMPULSE = decimal(0.2);
const decimal ContactSolver::SLOP= decimal(0.01);

// Constructor
ContactSolver::ContactSolver(const std::map<RigidBody*, uint>& mapBodyToVelocityIndex,
                             SingleFrameAllocator& allocator)
              :mSplitLinearVelocities(nullptr), mSplitAngularVelocities(nullptr),
               mContactConstraints(nullptr), mSingleFrameAllocator(allocator),
               mLinearVelocities(nullptr), mAngularVelocities(nullptr),
               mMapBodyToConstrainedVelocityIndex(mapBodyToVelocityIndex),
               mIsSplitImpulseActive(true) {

}

// Initialize the constraint solver for a given island
void ContactSolver::initializeForIsland(decimal dt, Island* island) {

    PROFILE("ContactSolver::initializeForIsland()");

    assert(island != nullptr);
    assert(island->getNbBodies() > 0);
    assert(island->getNbContactManifolds() > 0);
    assert(mSplitLinearVelocities != nullptr);
    assert(mSplitAngularVelocities != nullptr);

    // Set the current time step
    mTimeStep = dt;

    mNbContactManifolds = island->getNbContactManifolds();

    mContactConstraints = new ContactManifoldSolver[mNbContactManifolds];
    assert(mContactConstraints != nullptr);

    // For each contact manifold of the island
    ContactManifold** contactManifolds = island->getContactManifolds();
    for (uint i=0; i<mNbContactManifolds; i++) {

        ContactManifold* externalManifold = contactManifolds[i];

        ContactManifoldSolver& internalManifold = mContactConstraints[i];

        assert(externalManifold->getNbContactPoints() > 0);

        // Get the two bodies of the contact
        RigidBody* body1 = static_cast<RigidBody*>(externalManifold->getContactPoint(0)->getBody1());
        RigidBody* body2 = static_cast<RigidBody*>(externalManifold->getContactPoint(0)->getBody2());
        assert(body1 != nullptr);
        assert(body2 != nullptr);

        // Get the position of the two bodies
        const Vector3& x1 = body1->mCenterOfMassWorld;
        const Vector3& x2 = body2->mCenterOfMassWorld;

        // Initialize the internal contact manifold structure using the external
        // contact manifold
        internalManifold.indexBody1 = mMapBodyToConstrainedVelocityIndex.find(body1)->second;
        internalManifold.indexBody2 = mMapBodyToConstrainedVelocityIndex.find(body2)->second;
        internalManifold.inverseInertiaTensorBody1 = body1->getInertiaTensorInverseWorld();
        internalManifold.inverseInertiaTensorBody2 = body2->getInertiaTensorInverseWorld();
        internalManifold.massInverseBody1 = body1->mMassInverse;
        internalManifold.massInverseBody2 = body2->mMassInverse;
        internalManifold.nbContacts = externalManifold->getNbContactPoints();
        internalManifold.restitutionFactor = computeMixedRestitutionFactor(body1, body2);
        internalManifold.frictionCoefficient = computeMixedFrictionCoefficient(body1, body2);
        internalManifold.rollingResistanceFactor = computeMixedRollingResistance(body1, body2);
        internalManifold.externalContactManifold = externalManifold;
        internalManifold.isBody1DynamicType = body1->getType() == BodyType::DYNAMIC;
        internalManifold.isBody2DynamicType = body2->getType() == BodyType::DYNAMIC;
        internalManifold.normal.setToZero();
        internalManifold.frictionPointBody1 = Vector3::zero();
        internalManifold.frictionPointBody2 = Vector3::zero();

        // Get the velocities of the bodies
        const Vector3& v1 = mLinearVelocities[internalManifold.indexBody1];
        const Vector3& w1 = mAngularVelocities[internalManifold.indexBody1];
        const Vector3& v2 = mLinearVelocities[internalManifold.indexBody2];
        const Vector3& w2 = mAngularVelocities[internalManifold.indexBody2];

        // For each  contact point of the contact manifold
        for (uint c=0; c<externalManifold->getNbContactPoints(); c++) {

            ContactPointSolver& contactPoint = internalManifold.contacts[c];

            // Get a contact point
            ContactPoint* externalContact = externalManifold->getContactPoint(c);

            // Get the contact point on the two bodies
            Vector3 p1 = externalContact->getWorldPointOnBody1();
            Vector3 p2 = externalContact->getWorldPointOnBody2();

            contactPoint.externalContact = externalContact;
            contactPoint.normal = externalContact->getNormal();
            contactPoint.r1 = p1 - x1;
            contactPoint.r2 = p2 - x2;
            contactPoint.penetrationDepth = externalContact->getPenetrationDepth();
            contactPoint.isRestingContact = externalContact->getIsRestingContact();
            externalContact->setIsRestingContact(true);
            contactPoint.oldFrictionVector1 = externalContact->getFrictionVector1();
            contactPoint.oldFrictionVector2 = externalContact->getFrictionVector2();
            contactPoint.penetrationImpulse = externalContact->getPenetrationImpulse();
            contactPoint.penetrationSplitImpulse = 0.0;
            contactPoint.friction1Impulse = externalContact->getFrictionImpulse1();
            contactPoint.friction2Impulse = externalContact->getFrictionImpulse2();
            contactPoint.rollingResistanceImpulse = externalContact->getRollingResistanceImpulse();

            internalManifold.frictionPointBody1 += p1;
            internalManifold.frictionPointBody2 += p2;

            // Compute the velocity difference
            Vector3 deltaV = v2 + w2.cross(contactPoint.r2) - v1 - w1.cross(contactPoint.r1);

            contactPoint.r1CrossN = contactPoint.r1.cross(contactPoint.normal);
            contactPoint.r2CrossN = contactPoint.r2.cross(contactPoint.normal);

            // Compute the inverse mass matrix K for the penetration constraint
            decimal massPenetration = internalManifold.massInverseBody1 + internalManifold.massInverseBody2 +
                    ((internalManifold.inverseInertiaTensorBody1 * contactPoint.r1CrossN).cross(contactPoint.r1)).dot(contactPoint.normal) +
                    ((internalManifold.inverseInertiaTensorBody2 * contactPoint.r2CrossN).cross(contactPoint.r2)).dot(contactPoint.normal);
            contactPoint.inversePenetrationMass = massPenetration > decimal(0.0) ? decimal(1.0) / massPenetration : decimal(0.0);

            // Compute the restitution velocity bias "b". We compute this here instead
            // of inside the solve() method because we need to use the velocity difference
            // at the beginning of the contact. Note that if it is a resting contact (normal
            // velocity bellow a given threshold), we do not add a restitution velocity bias
            contactPoint.restitutionBias = 0.0;
            decimal deltaVDotN = deltaV.dot(contactPoint.normal);
            if (deltaVDotN < -RESTITUTION_VELOCITY_THRESHOLD) {
                contactPoint.restitutionBias = internalManifold.restitutionFactor * deltaVDotN;
            }

            internalManifold.normal += contactPoint.normal;
        }


        internalManifold.frictionPointBody1 /=static_cast<decimal>(internalManifold.nbContacts);
        internalManifold.frictionPointBody2 /=static_cast<decimal>(internalManifold.nbContacts);
        internalManifold.r1Friction = internalManifold.frictionPointBody1 - x1;
        internalManifold.r2Friction = internalManifold.frictionPointBody2 - x2;
        internalManifold.oldFrictionVector1 = externalManifold->getFrictionVector1();
        internalManifold.oldFrictionVector2 = externalManifold->getFrictionVector2();

        // Initialize the accumulated impulses with the previous step accumulated impulses
        internalManifold.friction1Impulse = externalManifold->getFrictionImpulse1();
        internalManifold.friction2Impulse = externalManifold->getFrictionImpulse2();
        internalManifold.frictionTwistImpulse = externalManifold->getFrictionTwistImpulse();

        // Compute the inverse K matrix for the rolling resistance constraint
        internalManifold.inverseRollingResistance.setToZero();
        if (internalManifold.rollingResistanceFactor > 0 && (internalManifold.isBody1DynamicType || internalManifold.isBody2DynamicType)) {
            internalManifold.inverseRollingResistance = internalManifold.inverseInertiaTensorBody1 + internalManifold.inverseInertiaTensorBody2;
            internalManifold.inverseRollingResistance = internalManifold.inverseRollingResistance.getInverse();
        }

        internalManifold.normal.normalize();

        Vector3 deltaVFrictionPoint = v2 + w2.cross(internalManifold.r2Friction) -
                                      v1 - w1.cross(internalManifold.r1Friction);

        // Compute the friction vectors
        computeFrictionVectors(deltaVFrictionPoint, internalManifold);

        // Compute the inverse mass matrix K for the friction constraints at the center of
        // the contact manifold
        internalManifold.r1CrossT1 = internalManifold.r1Friction.cross(internalManifold.frictionVector1);
        internalManifold.r1CrossT2 = internalManifold.r1Friction.cross(internalManifold.frictionVector2);
        internalManifold.r2CrossT1 = internalManifold.r2Friction.cross(internalManifold.frictionVector1);
        internalManifold.r2CrossT2 = internalManifold.r2Friction.cross(internalManifold.frictionVector2);
        decimal friction1Mass = internalManifold.massInverseBody1 + internalManifold.massInverseBody2 +
                                ((internalManifold.inverseInertiaTensorBody1 * internalManifold.r1CrossT1).cross(internalManifold.r1Friction)).dot(
                                internalManifold.frictionVector1) +
                                ((internalManifold.inverseInertiaTensorBody2 * internalManifold.r2CrossT1).cross(internalManifold.r2Friction)).dot(
                                internalManifold.frictionVector1);
        decimal friction2Mass = internalManifold.massInverseBody1 + internalManifold.massInverseBody2 +
                                ((internalManifold.inverseInertiaTensorBody1 * internalManifold.r1CrossT2).cross(internalManifold.r1Friction)).dot(
                                internalManifold.frictionVector2) +
                                ((internalManifold.inverseInertiaTensorBody2 * internalManifold.r2CrossT2).cross(internalManifold.r2Friction)).dot(
                                internalManifold.frictionVector2);
        decimal frictionTwistMass = internalManifold.normal.dot(internalManifold.inverseInertiaTensorBody1 *
                                       internalManifold.normal) +
                                    internalManifold.normal.dot(internalManifold.inverseInertiaTensorBody2 *
                                       internalManifold.normal);
        internalManifold.inverseFriction1Mass = friction1Mass > decimal(0.0) ? decimal(1.0) / friction1Mass : decimal(0.0);
        internalManifold.inverseFriction2Mass = friction2Mass > decimal(0.0) ? decimal(1.0) / friction2Mass : decimal(0.0);
        internalManifold.inverseTwistFrictionMass = frictionTwistMass > decimal(0.0) ? decimal(1.0) / frictionTwistMass : decimal(0.0);
    }
}

// Warm start the solver.
/// For each constraint, we apply the previous impulse (from the previous step)
/// at the beginning. With this technique, we will converge faster towards
/// the solution of the linear system
void ContactSolver::warmStart() {

    // For each constraint
    for (uint c=0; c<mNbContactManifolds; c++) {

        ContactManifoldSolver& contactManifold = mContactConstraints[c];

        bool atLeastOneRestingContactPoint = false;

        for (uint i=0; i<contactManifold.nbContacts; i++) {

            ContactPointSolver& contactPoint = contactManifold.contacts[i];

            // If it is not a new contact (this contact was already existing at last time step)
            if (contactPoint.isRestingContact) {

                atLeastOneRestingContactPoint = true;

                // --------- Penetration --------- //

                // Update the velocities of the body 1 by applying the impulse P
                Vector3 impulsePenetration = contactPoint.normal * contactPoint.penetrationImpulse;
                mLinearVelocities[contactManifold.indexBody1] += contactManifold.massInverseBody1 * (-impulsePenetration);
                mAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 * (-contactPoint.r1CrossN * contactPoint.penetrationImpulse);

                // Update the velocities of the body 2 by applying the impulse P
                mLinearVelocities[contactManifold.indexBody2] += contactManifold.massInverseBody2 * impulsePenetration;
                mAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 * (contactPoint.r2CrossN * contactPoint.penetrationImpulse);
            }
            else {  // If it is a new contact point

                // Initialize the accumulated impulses to zero
                contactPoint.penetrationImpulse = 0.0;
                contactPoint.friction1Impulse = 0.0;
                contactPoint.friction2Impulse = 0.0;
                contactPoint.rollingResistanceImpulse = Vector3::zero();
            }
        }

        // If we solve the friction constraints at the center of the contact manifold and there is
        // at least one resting contact point in the contact manifold
        if (atLeastOneRestingContactPoint) {

            // Project the old friction impulses (with old friction vectors) into the new friction
            // vectors to get the new friction impulses
            Vector3 oldFrictionImpulse = contactManifold.friction1Impulse * contactManifold.oldFrictionVector1 +
                                         contactManifold.friction2Impulse * contactManifold.oldFrictionVector2;
            contactManifold.friction1Impulse = oldFrictionImpulse.dot(contactManifold.frictionVector1);
            contactManifold.friction2Impulse = oldFrictionImpulse.dot(contactManifold.frictionVector2);

            // ------ First friction constraint at the center of the contact manifold ------ //

            // Compute the impulse P = J^T * lambda
            Vector3 angularImpulseBody1 = -contactManifold.r1CrossT1 *
                                           contactManifold.friction1Impulse;
            Vector3 linearImpulseBody2 = contactManifold.frictionVector1 *
                                         contactManifold.friction1Impulse;
            Vector3 angularImpulseBody2 = contactManifold.r2CrossT1 *
                                          contactManifold.friction1Impulse;

            // Update the velocities of the body 1 by applying the impulse P
            mLinearVelocities[contactManifold.indexBody1] += contactManifold.massInverseBody1 * (-linearImpulseBody2);
            mAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 * angularImpulseBody1;

            // Update the velocities of the body 1 by applying the impulse P
            mLinearVelocities[contactManifold.indexBody2] += contactManifold.massInverseBody2 * linearImpulseBody2;
            mAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 * angularImpulseBody2;

            // ------ Second friction constraint at the center of the contact manifold ----- //

            // Compute the impulse P = J^T * lambda
            angularImpulseBody1 = -contactManifold.r1CrossT2 * contactManifold.friction2Impulse;
            linearImpulseBody2 = contactManifold.frictionVector2 * contactManifold.friction2Impulse;
            angularImpulseBody2 = contactManifold.r2CrossT2 * contactManifold.friction2Impulse;

            // Update the velocities of the body 1 by applying the impulse P
            mLinearVelocities[contactManifold.indexBody1] += contactManifold.massInverseBody1 * (-linearImpulseBody2);
            mAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 * angularImpulseBody1;

            // Update the velocities of the body 2 by applying the impulse P
            mLinearVelocities[contactManifold.indexBody2] += contactManifold.massInverseBody2 * linearImpulseBody2;
            mAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 * angularImpulseBody2;

            // ------ Twist friction constraint at the center of the contact manifold ------ //

            // Compute the impulse P = J^T * lambda
            angularImpulseBody1 = -contactManifold.normal * contactManifold.frictionTwistImpulse;
            angularImpulseBody2 = contactManifold.normal * contactManifold.frictionTwistImpulse;

            // Update the velocities of the body 1 by applying the impulse P
            mAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 * angularImpulseBody1;

            // Update the velocities of the body 2 by applying the impulse P
            mAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 * angularImpulseBody2;

            // ------ Rolling resistance at the center of the contact manifold ------ //

            // Compute the impulse P = J^T * lambda
            angularImpulseBody2 = contactManifold.rollingResistanceImpulse;

            // Update the velocities of the body 1 by applying the impulse P
            mAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 * (-angularImpulseBody2);

            // Update the velocities of the body 1 by applying the impulse P
            mAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 * angularImpulseBody2;
        }
        else {  // If it is a new contact manifold

            // Initialize the accumulated impulses to zero
            contactManifold.friction1Impulse = 0.0;
            contactManifold.friction2Impulse = 0.0;
            contactManifold.frictionTwistImpulse = 0.0;
            contactManifold.rollingResistanceImpulse = Vector3::zero();
        }
    }
}

// Solve the contacts
void ContactSolver::solve() {

    PROFILE("ContactSolver::solve()");

    decimal deltaLambda;
    decimal lambdaTemp;

    // For each contact manifold
    for (uint c=0; c<mNbContactManifolds; c++) {

        ContactManifoldSolver& contactManifold = mContactConstraints[c];

        decimal sumPenetrationImpulse = 0.0;

        // Get the constrained velocities
        const Vector3& v1 = mLinearVelocities[contactManifold.indexBody1];
        const Vector3& w1 = mAngularVelocities[contactManifold.indexBody1];
        const Vector3& v2 = mLinearVelocities[contactManifold.indexBody2];
        const Vector3& w2 = mAngularVelocities[contactManifold.indexBody2];

        for (uint i=0; i<contactManifold.nbContacts; i++) {

            ContactPointSolver& contactPoint = contactManifold.contacts[i];

            // --------- Penetration --------- //

            // Compute J*v
            Vector3 deltaV = v2 + w2.cross(contactPoint.r2) - v1 - w1.cross(contactPoint.r1);
            decimal deltaVDotN = deltaV.dot(contactPoint.normal);
            decimal Jv = deltaVDotN;

            // Compute the bias "b" of the constraint
            decimal beta = mIsSplitImpulseActive ? BETA_SPLIT_IMPULSE : BETA;
            decimal biasPenetrationDepth = 0.0;
            if (contactPoint.penetrationDepth > SLOP) biasPenetrationDepth = -(beta/mTimeStep) *
                    max(0.0f, float(contactPoint.penetrationDepth - SLOP));
            decimal b = biasPenetrationDepth + contactPoint.restitutionBias;

            // Compute the Lagrange multiplier lambda
            if (mIsSplitImpulseActive) {
                deltaLambda = - (Jv + contactPoint.restitutionBias) *
                        contactPoint.inversePenetrationMass;
            }
            else {
                deltaLambda = - (Jv + b) * contactPoint.inversePenetrationMass;
            }
            lambdaTemp = contactPoint.penetrationImpulse;
            contactPoint.penetrationImpulse = std::max(contactPoint.penetrationImpulse +
                                                       deltaLambda, decimal(0.0));
            deltaLambda = contactPoint.penetrationImpulse - lambdaTemp;

            Vector3 linearImpulse = contactPoint.normal * deltaLambda;

            // Update the velocities of the body 1 by applying the impulse P
            mLinearVelocities[contactManifold.indexBody1] += contactManifold.massInverseBody1 * (-linearImpulse);
            mAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 * (-contactPoint.r1CrossN * deltaLambda);

            // Update the velocities of the body 2 by applying the impulse P
            mLinearVelocities[contactManifold.indexBody2] += contactManifold.massInverseBody2 * linearImpulse;
            mAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 * (contactPoint.r2CrossN * deltaLambda);

            sumPenetrationImpulse += contactPoint.penetrationImpulse;

            // If the split impulse position correction is active
            if (mIsSplitImpulseActive) {

                // Split impulse (position correction)
                const Vector3& v1Split = mSplitLinearVelocities[contactManifold.indexBody1];
                const Vector3& w1Split = mSplitAngularVelocities[contactManifold.indexBody1];
                const Vector3& v2Split = mSplitLinearVelocities[contactManifold.indexBody2];
                const Vector3& w2Split = mSplitAngularVelocities[contactManifold.indexBody2];
                Vector3 deltaVSplit = v2Split + w2Split.cross(contactPoint.r2) -
                        v1Split - w1Split.cross(contactPoint.r1);
                decimal JvSplit = deltaVSplit.dot(contactPoint.normal);
                decimal deltaLambdaSplit = - (JvSplit + biasPenetrationDepth) *
                        contactPoint.inversePenetrationMass;
                decimal lambdaTempSplit = contactPoint.penetrationSplitImpulse;
                contactPoint.penetrationSplitImpulse = std::max(
                            contactPoint.penetrationSplitImpulse +
                            deltaLambdaSplit, decimal(0.0));
                deltaLambdaSplit = contactPoint.penetrationSplitImpulse - lambdaTempSplit;

                Vector3 linearImpulse = contactPoint.normal * deltaLambdaSplit;

                // Update the velocities of the body 1 by applying the impulse P
                mSplitLinearVelocities[contactManifold.indexBody1] += contactManifold.massInverseBody1 * (-linearImpulse);
                mSplitAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 *
                                                                       (-contactPoint.r1CrossN * deltaLambdaSplit);

                // Update the velocities of the body 1 by applying the impulse P
                mSplitLinearVelocities[contactManifold.indexBody2] += contactManifold.massInverseBody2 * linearImpulse;
                mSplitAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 *
                                                                       contactPoint.r2CrossN * deltaLambdaSplit;
            }
        }

        // ------ First friction constraint at the center of the contact manifol ------ //

        // Compute J*v
        Vector3 deltaV = v2 + w2.cross(contactManifold.r2Friction)
                - v1 - w1.cross(contactManifold.r1Friction);
        decimal Jv = deltaV.dot(contactManifold.frictionVector1);

        // Compute the Lagrange multiplier lambda
        decimal deltaLambda = -Jv * contactManifold.inverseFriction1Mass;
        decimal frictionLimit = contactManifold.frictionCoefficient * sumPenetrationImpulse;
        lambdaTemp = contactManifold.friction1Impulse;
        contactManifold.friction1Impulse = std::max(-frictionLimit,
                                                    std::min(contactManifold.friction1Impulse +
                                                             deltaLambda, frictionLimit));
        deltaLambda = contactManifold.friction1Impulse - lambdaTemp;

        // Compute the impulse P=J^T * lambda
        Vector3 angularImpulseBody1 = -contactManifold.r1CrossT1 * deltaLambda;
        Vector3 linearImpulseBody2 = contactManifold.frictionVector1 * deltaLambda;
        Vector3 angularImpulseBody2 = contactManifold.r2CrossT1 * deltaLambda;

        // Update the velocities of the body 1 by applying the impulse P
        mLinearVelocities[contactManifold.indexBody1] += contactManifold.massInverseBody1 * (-linearImpulseBody2);
        mAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 * angularImpulseBody1;

        // Update the velocities of the body 2 by applying the impulse P
        mLinearVelocities[contactManifold.indexBody2] += contactManifold.massInverseBody2 * linearImpulseBody2;
        mAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 * angularImpulseBody2;

        // ------ Second friction constraint at the center of the contact manifol ----- //

        // Compute J*v
        deltaV = v2 + w2.cross(contactManifold.r2Friction) - v1 - w1.cross(contactManifold.r1Friction);
        Jv = deltaV.dot(contactManifold.frictionVector2);

        // Compute the Lagrange multiplier lambda
        deltaLambda = -Jv * contactManifold.inverseFriction2Mass;
        frictionLimit = contactManifold.frictionCoefficient * sumPenetrationImpulse;
        lambdaTemp = contactManifold.friction2Impulse;
        contactManifold.friction2Impulse = std::max(-frictionLimit,
                                                    std::min(contactManifold.friction2Impulse +
                                                             deltaLambda, frictionLimit));
        deltaLambda = contactManifold.friction2Impulse - lambdaTemp;

        // Compute the impulse P=J^T * lambda
        angularImpulseBody1 = -contactManifold.r1CrossT2 * deltaLambda;
        linearImpulseBody2 = contactManifold.frictionVector2 * deltaLambda;
        angularImpulseBody2 = contactManifold.r2CrossT2 * deltaLambda;

        // Update the velocities of the body 1 by applying the impulse P
        mLinearVelocities[contactManifold.indexBody1] += contactManifold.massInverseBody1 * (-linearImpulseBody2);
        mAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 * angularImpulseBody1;

        // Update the velocities of the body 2 by applying the impulse P
        mLinearVelocities[contactManifold.indexBody2] += contactManifold.massInverseBody2 * linearImpulseBody2;
        mAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 * angularImpulseBody2;

        // ------ Twist friction constraint at the center of the contact manifol ------ //

        // Compute J*v
        deltaV = w2 - w1;
        Jv = deltaV.dot(contactManifold.normal);

        deltaLambda = -Jv * (contactManifold.inverseTwistFrictionMass);
        frictionLimit = contactManifold.frictionCoefficient * sumPenetrationImpulse;
        lambdaTemp = contactManifold.frictionTwistImpulse;
        contactManifold.frictionTwistImpulse = std::max(-frictionLimit,
                                                        std::min(contactManifold.frictionTwistImpulse
                                                                 + deltaLambda, frictionLimit));
        deltaLambda = contactManifold.frictionTwistImpulse - lambdaTemp;

        // Compute the impulse P=J^T * lambda
        angularImpulseBody2 = contactManifold.normal * deltaLambda;

        // Update the velocities of the body 1 by applying the impulse P
        mAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 * (-angularImpulseBody2);

        // Update the velocities of the body 1 by applying the impulse P
        mAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 * angularImpulseBody2;

        // --------- Rolling resistance constraint at the center of the contact manifold --------- //

        if (contactManifold.rollingResistanceFactor > 0) {

            // Compute J*v
            const Vector3 JvRolling = w2 - w1;

            // Compute the Lagrange multiplier lambda
            Vector3 deltaLambdaRolling = contactManifold.inverseRollingResistance * (-JvRolling);
            decimal rollingLimit = contactManifold.rollingResistanceFactor * sumPenetrationImpulse;
            Vector3 lambdaTempRolling = contactManifold.rollingResistanceImpulse;
            contactManifold.rollingResistanceImpulse = clamp(contactManifold.rollingResistanceImpulse +
                                                                 deltaLambdaRolling, rollingLimit);
            deltaLambdaRolling = contactManifold.rollingResistanceImpulse - lambdaTempRolling;

            // Update the velocities of the body 1 by applying the impulse P
            mAngularVelocities[contactManifold.indexBody1] += contactManifold.inverseInertiaTensorBody1 * (-deltaLambdaRolling);

            // Update the velocities of the body 2 by applying the impulse P
            mAngularVelocities[contactManifold.indexBody2] += contactManifold.inverseInertiaTensorBody2 * deltaLambdaRolling;
        }
    }
}

// Store the computed impulses to use them to
// warm start the solver at the next iteration
void ContactSolver::storeImpulses() {

    // For each contact manifold
    for (uint c=0; c<mNbContactManifolds; c++) {

        ContactManifoldSolver& manifold = mContactConstraints[c];

        for (uint i=0; i<manifold.nbContacts; i++) {

            ContactPointSolver& contactPoint = manifold.contacts[i];

            contactPoint.externalContact->setPenetrationImpulse(contactPoint.penetrationImpulse);
            contactPoint.externalContact->setFrictionImpulse1(contactPoint.friction1Impulse);
            contactPoint.externalContact->setFrictionImpulse2(contactPoint.friction2Impulse);
            contactPoint.externalContact->setRollingResistanceImpulse(contactPoint.rollingResistanceImpulse);

            contactPoint.externalContact->setFrictionVector1(contactPoint.frictionVector1);
            contactPoint.externalContact->setFrictionVector2(contactPoint.frictionVector2);
        }

        manifold.externalContactManifold->setFrictionImpulse1(manifold.friction1Impulse);
        manifold.externalContactManifold->setFrictionImpulse2(manifold.friction2Impulse);
        manifold.externalContactManifold->setFrictionTwistImpulse(manifold.frictionTwistImpulse);
        manifold.externalContactManifold->setRollingResistanceImpulse(manifold.rollingResistanceImpulse);
        manifold.externalContactManifold->setFrictionVector1(manifold.frictionVector1);
        manifold.externalContactManifold->setFrictionVector2(manifold.frictionVector2);
    }
}

// Compute the two unit orthogonal vectors "t1" and "t2" that span the tangential friction plane
// for a contact point. The two vectors have to be such that : t1 x t2 = contactNormal.
void ContactSolver::computeFrictionVectors(const Vector3& deltaVelocity,
                                           ContactPointSolver& contactPoint) const {

    assert(contactPoint.normal.length() > 0.0);

    // Compute the velocity difference vector in the tangential plane
    Vector3 normalVelocity = deltaVelocity.dot(contactPoint.normal) * contactPoint.normal;
    Vector3 tangentVelocity = deltaVelocity - normalVelocity;

    // If the velocty difference in the tangential plane is not zero
    decimal lengthTangenVelocity = tangentVelocity.length();
    if (lengthTangenVelocity > MACHINE_EPSILON) {

        // Compute the first friction vector in the direction of the tangent
        // velocity difference
        contactPoint.frictionVector1 = tangentVelocity / lengthTangenVelocity;
    }
    else {

        // Get any orthogonal vector to the normal as the first friction vector
        contactPoint.frictionVector1 = contactPoint.normal.getOneUnitOrthogonalVector();
    }

    // The second friction vector is computed by the cross product of the firs
    // friction vector and the contact normal
    contactPoint.frictionVector2 =contactPoint.normal.cross(contactPoint.frictionVector1).getUnit();
}

// Compute the two unit orthogonal vectors "t1" and "t2" that span the tangential friction plane
// for a contact manifold. The two vectors have to be such that : t1 x t2 = contactNormal.
void ContactSolver::computeFrictionVectors(const Vector3& deltaVelocity,
                                           ContactManifoldSolver& contact) const {

    assert(contact.normal.length() > decimal(0.0));

    // Compute the velocity difference vector in the tangential plane
    Vector3 normalVelocity = deltaVelocity.dot(contact.normal) * contact.normal;
    Vector3 tangentVelocity = deltaVelocity - normalVelocity;

    // If the velocty difference in the tangential plane is not zero
    decimal lengthTangenVelocity = tangentVelocity.length();
    if (lengthTangenVelocity > MACHINE_EPSILON) {

        // Compute the first friction vector in the direction of the tangent
        // velocity difference
        contact.frictionVector1 = tangentVelocity / lengthTangenVelocity;
    }
    else {

        // Get any orthogonal vector to the normal as the first friction vector
        contact.frictionVector1 = contact.normal.getOneUnitOrthogonalVector();
    }

    // The second friction vector is computed by the cross product of the firs
    // friction vector and the contact normal
    contact.frictionVector2 = contact.normal.cross(contact.frictionVector1).getUnit();
}

// Clean up the constraint solver
void ContactSolver::cleanup() {

    if (mContactConstraints != nullptr) {
        delete[] mContactConstraints;
        mContactConstraints = nullptr;
    }
}
