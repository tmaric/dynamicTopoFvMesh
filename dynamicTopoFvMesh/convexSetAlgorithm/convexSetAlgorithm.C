/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

Class
    convexSetAlgorithm

Description
    Base class for convexSetAlgorithms

Author
    Sandeep Menon
    University of Massachusetts Amherst
    All rights reserved

\*---------------------------------------------------------------------------*/

#include "Time.H"
#include "IOMap.H"
#include "meshOps.H"
#include "polyMesh.H"
#include "objectMap.H"
#include "edgeIOList.H"
#include "cellIOList.H"
#include "demandDrivenData.H"

#include "convexSetAlgorithm.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

defineTypeName(convexSetAlgorithm::mappingTreeData);

// * * * * * * * * * * * * * Protected Member Functions  * * * * * * * * * * //

// Return a const reference to the search tree
const convexSetAlgorithm::SearchTreeType& convexSetAlgorithm::searchTree() const
{
    if (!searchTreePtr_)
    {
        constructSearchTree();
    }

    return *searchTreePtr_;
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

// Construct from components
convexSetAlgorithm::convexSetAlgorithm
(
    const polyMesh& mesh,
    const pointField& newPoints,
    const UList<edge>& newEdges,
    const UList<face>& newFaces,
    const UList<cell>& newCells,
    const UList<label>& newOwner,
    const UList<label>& newNeighbour
)
:
    nOldPoints_(mesh.nPoints()),
    mesh_(mesh),
    newPoints_(newPoints),
    newEdges_(newEdges),
    newFaces_(newFaces),
    newCells_(newCells),
    newOwner_(newOwner),
    newNeighbour_(newNeighbour),
    random_(std::time(0)),
    searchTreePtr_(NULL)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

convexSetAlgorithm::~convexSetAlgorithm()
{
    deleteDemandDrivenData(searchTreePtr_);
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

// Obtain map weighting factors
void convexSetAlgorithm::computeWeights
(
    const label index,
    const label offset,
    const labelList& mapCandidates,
    const labelListList& neighbourList,
    labelList& parents,
    scalarField& weights,
    vectorField& centres,
    bool output
) const
{
    if (parents.size() || weights.size() || centres.size())
    {
        FatalErrorIn
        (
            "\n\n"
            "void convexSetAlgorithm::computeWeights\n"
            "(\n"
            "    const label index,\n"
            "    const label offset,\n"
            "    const labelList& mapCandidates,\n"
            "    const labelListList& neighbourList,\n"
            "    labelList& parents,\n"
            "    scalarField& weights,\n"
            "    vectorField& centres,\n"
            "    bool output\n"
            ") const\n"
        )
            << " Addressing has already been calculated." << nl
            << " Index: " << index << nl
            << " Offset: " << offset << nl
            << " mapCandidates: " << mapCandidates << nl
            << " Type: " << (dimension() == 2 ? "Face" : "Cell") << nl
            << " Parents: " << parents << nl
            << " Weights: " << weights << nl
            << " Centres: " << centres << nl
            << abort(FatalError);
    }

    // Do nothing for empty lists
    if (mapCandidates.empty() || neighbourList.empty())
    {
        return;
    }

    bool changed;
    label nAttempts = 0, nIntersects = 0;

    const label neighbourListSize = (neighbourList.size() + offset);

    // Maintain a check-list
    labelHashSet checked, skipped;

    // Loop and add intersections until nothing changes
    do
    {
        // Reset flag
        changed = false;

        // Fetch the set of candidates
        labelList checkList;

        if (nAttempts == 0)
        {
            checkList = mapCandidates;
        }
        else
        {
            checkList = checked.toc();
        }

        forAll(checkList, indexI)
        {
            labelList checkEntities;

            if (nAttempts == 0)
            {
                const label mapCandidate = checkList[indexI];

                // Prevent accidental addressing out of bounds
                if (mapCandidate < offset || mapCandidate >= neighbourListSize)
                {
                    continue;
                }
                else
                {
                    checkEntities = labelList(1, mapCandidate - offset);
                }
            }
            else
            {
                checkEntities = neighbourList[checkList[indexI]];
            }

            forAll(checkEntities, entityI)
            {
                label checkEntity = checkEntities[entityI];

                // Skip if this is already
                // on the checked / skipped list
                if
                (
                    (checked.found(checkEntity)) ||
                    (skipped.found(checkEntity))
                )
                {
                    continue;
                }

                bool intersect =
                (
                    computeIntersection
                    (
                        index,
                        checkEntity + offset,
                        offset,
                        output
                    )
                );

                if (intersect)
                {
                    nIntersects++;

                    if (!checked.found(checkEntity))
                    {
                        checked.insert(checkEntity);
                    }

                    changed = true;
                }
                else
                {
                    // Add to the skipped list
                    if (!skipped.found(checkEntity))
                    {
                        skipped.insert(checkEntity);
                    }
                }
            }
        }

        if (nAttempts == 0 && !changed)
        {
            // Need to setup a rescue mechanism.
            labelHashSet rescue;

            forAll(mapCandidates, cI)
            {
                const label mapCandidate = checkList[cI];

                // Prevent accidental addressing out of bounds
                if (mapCandidate < offset || mapCandidate >= neighbourListSize)
                {
                    continue;
                }

                if (!rescue.found(mapCandidate - offset))
                {
                    rescue.insert(mapCandidate - offset);
                }
            }

            for (label level = 0; level < 10; level++)
            {
                labelList initList = rescue.toc();

                forAll(initList, fI)
                {
                    const labelList& ff = neighbourList[initList[fI]];

                    forAll(ff, entityI)
                    {
                        if (!rescue.found(ff[entityI]))
                        {
                            rescue.insert(ff[entityI]);
                        }
                    }
                }
            }

            labelList finalList = rescue.toc();

            forAll(finalList, entityI)
            {
                label checkEntity = finalList[entityI];

                bool intersect =
                (
                    computeIntersection
                    (
                        index,
                        checkEntity + offset,
                        offset,
                        output
                    )
                );

                if (intersect)
                {
                    nIntersects++;

                    if (!checked.found(checkEntity))
                    {
                        checked.insert(checkEntity);
                    }

                    changed = true;
                    break;
                }
            }

            if (!changed)
            {
                // No point in continuing further...
                break;
            }
        }

        nAttempts++;

        // Break out if we're taking too long
        if (nAttempts > 20)
        {
            break;
        }

    } while (changed);

    // Normalize weights
    normalize(false);

    // Populate lists
    populateLists(parents, centres, weights);
}


// Return true if accumulated weights are consistent
bool convexSetAlgorithm::consistent(const scalar tolerance) const
{
    if (weights_.size())
    {
        scalar normError = mag(1.0 - (sum(weights_)/normFactor_));

        if (normError < tolerance)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    return false;
}


// Set the reference normal
void convexSetAlgorithm::setRefNorm(const vector& refNorm) const
{
    refNorm_ = refNorm;
}


// Set the reference centre using a point index
void convexSetAlgorithm::setRefCentre(const label pointIndex) const
{
    refCentre_ = newPoints_[pointIndex];
}


// Set the reference centre
void convexSetAlgorithm::setRefCentre(const vector& refCentre) const
{
    refCentre_ = refCentre;
}


// Set the normFactor
void convexSetAlgorithm::setNormFactor(const scalar normFactor) const
{
    normFactor_ = normFactor;
}


// Return the refNorm
const vector& convexSetAlgorithm::refNorm() const
{
    return refNorm_;
}


// Return the refCentre
const vector& convexSetAlgorithm::refCentre() const
{
    return refCentre_;
}


// Return the normFactor
scalar convexSetAlgorithm::normFactor() const
{
    return normFactor_;
}


// Normalize stored weights
void convexSetAlgorithm::normalize(const bool normSum) const
{
    if (normSum)
    {
        if (weights_.size())
        {
            weights_ /= sum(weights_);
        }
    }
    else
    {
        if (weights_.size())
        {
            weights_ /= normFactor_;
        }
    }
}


// Extract weights and centres to lists
void convexSetAlgorithm::populateLists
(
    labelList& parents,
    vectorField& centres,
    scalarField& weights
) const
{
    // Clear inputs
    parents.clear();
    centres.clear();
    weights.clear();

    if (weights_.size())
    {
        parents = parents_;
        centres = centres_;
        weights = weights_;
    }
}


// Write out connectivity information to disk
bool convexSetAlgorithm::write() const
{
    // TODO: port, TM.
    //pointIOField
    //(
        //IOobject
        //(
            //"newPoints",
            //mesh_.time().timeName(),
            //"convexSetAlgorithm",
            //mesh_,
            //IOobject::NO_READ,
            //IOobject::NO_WRITE,
            //false
        //),
        //newPoints_
    //).writeObject
    //(
        //IOstream::BINARY,
        //IOstream::currentVersion,
        //IOstream::COMPRESSED
    //);

    //edgeIOList
    //(
        //IOobject
        //(
            //"newEdges",
            //mesh_.time().timeName(),
            //"convexSetAlgorithm",
            //mesh_,
            //IOobject::NO_READ,
            //IOobject::NO_WRITE,
            //false
        //),
        //newEdges_
    //).writeObject
    //(
        //IOstream::BINARY,
        //IOstream::currentVersion,
        //IOstream::COMPRESSED
    //);

    //faceIOList
    //(
        //IOobject
        //(
            //"newFaces",
            //mesh_.time().timeName(),
            //"convexSetAlgorithm",
            //mesh_,
            //IOobject::NO_READ,
            //IOobject::NO_WRITE,
            //false
        //),
        //newFaces_
    //).writeObject
    //(
        //IOstream::BINARY,
        //IOstream::currentVersion,
        //IOstream::COMPRESSED
    //);

    //cellIOList
    //(
        //IOobject
        //(
            //"newCells",
            //mesh_.time().timeName(),
            //"convexSetAlgorithm",
            //mesh_,
            //IOobject::NO_READ,
            //IOobject::NO_WRITE,
            //false
        //),
        //newCells_
    //).writeObject
    //(
        //IOstream::BINARY,
        //IOstream::currentVersion,
        //IOstream::COMPRESSED
    //);

    //labelIOList
    //(
        //IOobject
        //(
            //"newOwner",
            //mesh_.time().timeName(),
            //"convexSetAlgorithm",
            //mesh_,
            //IOobject::NO_READ,
            //IOobject::NO_WRITE,
            //false
        //),
        //newOwner_
    //).writeObject
    //(
        //IOstream::BINARY,
        //IOstream::currentVersion,
        //IOstream::COMPRESSED
    //);

    //labelIOList
    //(
        //IOobject
        //(
            //"newNeighbour",
            //mesh_.time().timeName(),
            //"convexSetAlgorithm",
            //mesh_,
            //IOobject::NO_READ,
            //IOobject::NO_WRITE,
            //false
        //),
        //newNeighbour_
    //).writeObject
    //(
        //IOstream::BINARY,
        //IOstream::currentVersion,
        //IOstream::COMPRESSED
    //);

    return true;
}


// Output an entity as a VTK file
void convexSetAlgorithm::writeVTK
(
    const word& name,
    const label entity,
    const label primitiveType,
    const bool useOldConnectivity
) const
{
    writeVTK
    (
        name,
        labelList(1, entity),
        primitiveType,
        useOldConnectivity
    );
}


// Output a list of entities as a VTK file
void convexSetAlgorithm::writeVTK
(
    const word& name,
    const labelList& cList,
    const label primitiveType,
    const bool useOldConnectivity
) const
{
    if (useOldConnectivity)
    {
        const polyMesh& mesh = this->mesh_;

        meshOps::writeVTK
        (
            mesh,
            name,
            cList,
            primitiveType,
            mesh.points(),
            mesh.edges(),
            mesh.faces(),
            mesh.cells(),
            mesh.faceOwner()
        );
    }
    else
    {
        meshOps::writeVTK
        (
            this->mesh_,
            name,
            cList,
            primitiveType,
            newPoints_,
            newEdges_,
            newFaces_,
            newCells_,
            newOwner_
        );
    }
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// ************************************************************************* //
