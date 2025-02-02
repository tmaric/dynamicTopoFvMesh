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
    dynamicTopoFvMesh

Description
    Functions specific to conservative mapping

Author
    Sandeep Menon
    University of Massachusetts Amherst
    All rights reserved

\*---------------------------------------------------------------------------*/

#include "dynamicTopoFvMesh.H"

#include "Time.H"
#include "meshOps.H"
#include "IOmanip.H"
#include "triFace.H"
#include "objectMap.H"
#include "pointSetAlgorithm.H"
#include "faceSetAlgorithm.H"
#include "cellSetAlgorithm.H"

namespace Foam
{

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

defineTemplateTypeNameAndDebugWithName(IOList<objectMap>, "objectMapIOList", 0);

// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

// Compute mapping weights for modified entities
void dynamicTopoFvMesh::computeMapping
(
    const scalar matchTol,
    const bool skipMapping,
    const bool mappingOutput,
    const label pointStart,
    const label pointSize,
    const label faceStart,
    const label faceSize,
    const label cellStart,
    const label cellSize,
    const convexSetAlgorithm& pointAlgorithm,
    const convexSetAlgorithm& faceAlgorithm,
    const convexSetAlgorithm& cellAlgorithm
)
{
    labelList mapCandidates;
    label nInconsistencies = 0;
    scalar maxPointError = 0.0, maxFaceError = 0.0, maxCellError = 0.0;
    DynamicList<scalar> pointErrors(10), cellErrors(10), faceErrors(10);
    DynamicList<objectMap> failedPoints(10), failedCells(10), failedFaces(10);

    // Compute cell mapping
    for (label cellI = cellStart; cellI < (cellStart + cellSize); cellI++)
    {
        label cIndex = cellsFromCells_[cellI].index();
        labelList& masterObjects = cellsFromCells_[cellI].masterObjects();

        if (skipMapping)
        {
            // Dummy map from cell[0]
            masterObjects = labelList(1, 0);
            cellWeights_[cellI].setSize(1, 1.0);
            cellCentres_[cellI].setSize(1, vector::zero);
        }
        else
        {
            // Calculate the algorithm normFactor
            cellAlgorithm.computeNormFactor(cIndex);

            // Find the nearest candidates for mapping
            cellAlgorithm.findMappingCandidates(mapCandidates);

            // Obtain weighting factors for this cell.
            cellAlgorithm.computeWeights
            (
                cIndex,
                0,
                mapCandidates,
                polyMesh::cellCells(),
                masterObjects,
                cellWeights_[cellI],
                cellCentres_[cellI]
            );

            // Add contributions from subMeshes, if any.
            computeCoupledWeights
            (
                cIndex,
                cellAlgorithm.dimension(),
                masterObjects,
                cellWeights_[cellI],
                cellCentres_[cellI]
            );

            // Compute error
            scalar error = mag(1.0 - sum(cellWeights_[cellI]));

            if (error > matchTol)
            {
                bool consistent = false;

                // Check whether any edges lie on boundary patches.
                // These cells can have relaxed weights to account
                // for mild convexity.
                const cell& cellToCheck = cells_[cIndex];

                forAll(cellToCheck, fI)
                {
                    const labelList& fE = faceEdges_[cellToCheck[fI]];

                    forAll(fE, eI)
                    {
                        label eP = whichEdgePatch(fE[eI]);

                        // Disregard processor patches
                        if (getNeighbourProcessor(eP) > -1)
                        {
                            continue;
                        }

                        if (eP > -1)
                        {
                            consistent = true;
                            break;
                        }
                    }

                    if (consistent)
                    {
                        break;
                    }
                }

                if (!consistent)
                {
                    nInconsistencies++;

                    // Add to list
                    cellErrors.append(error);

                    // Accumulate error stats
                    maxCellError = Foam::max(maxCellError, error);

                    failedCells.append(objectMap(cIndex, labelList(0)));
                }
            }
        }
    }

    // Compute face mapping
    for (label faceI = faceStart; faceI < (faceStart + faceSize); faceI++)
    {
        label fIndex = facesFromFaces_[faceI].index();
        labelList& masterObjects = facesFromFaces_[faceI].masterObjects();

        label patchIndex = whichPatch(fIndex);
        label neiProc = getNeighbourProcessor(patchIndex);

        // Skip mapping for internal / processor faces.
        if (patchIndex == -1 || neiProc > -1)
        {
            // Set dummy masters, so that the conventional
            // faceMapper doesn't crash-and-burn
            masterObjects = labelList(1, 0);

            continue;
        }

        if (skipMapping)
        {
            // Dummy map from patch[0]
            masterObjects = labelList(1, 0);
            faceWeights_[faceI].setSize(1, 1.0);
            faceCentres_[faceI].setSize(1, vector::zero);
        }
        else
        {
            // Calculate the algorithm normFactor
            faceAlgorithm.computeNormFactor(fIndex);

            // Find the nearest candidates for mapping
            faceAlgorithm.findMappingCandidates(mapCandidates);

            // Obtain weighting factors for this face.
            faceAlgorithm.computeWeights
            (
                fIndex,
                boundaryMesh()[patchIndex].start(),
                mapCandidates,
                boundaryMesh()[patchIndex].faceFaces(),
                masterObjects,
                faceWeights_[faceI],
                faceCentres_[faceI]
            );

            // Add contributions from subMeshes, if any.
            computeCoupledWeights
            (
                fIndex,
                faceAlgorithm.dimension(),
                masterObjects,
                faceWeights_[faceI],
                faceCentres_[faceI]
            );

            // Compute error
            scalar error = mag(1.0 - sum(faceWeights_[faceI]));

            if (error > matchTol)
            {
                bool consistent = false;

                // Check whether any edges lie on bounding curves.
                // These faces can have relaxed weights to account
                // for addressing into patches on the other side
                // of the curve.
                const labelList& fEdges = faceEdges_[fIndex];

                forAll(fEdges, eI)
                {
                    if (checkBoundingCurve(fEdges[eI]))
                    {
                        consistent = true;
                    }
                }

                if (!consistent)
                {
                    nInconsistencies++;

                    // Add to list
                    faceErrors.append(error);

                    // Accumulate error stats
                    maxFaceError = Foam::max(maxFaceError, error);

                    failedFaces.append(objectMap(fIndex, labelList(0)));
                }
            }
        }
    }

    // Compute point mapping
    for (label pointI = pointStart; pointI < (pointStart + pointSize); pointI++)
    {
        label pIndex = pointsFromPoints_[pointI].index();
        labelList& masterObjects = pointsFromPoints_[pointI].masterObjects();

        if (skipMapping)
        {
            // Dummy map from point[0]
            masterObjects = labelList(1, 0);
            pointWeights_[pointI].setSize(1, 1.0);
        }
        else
        {
            const scalar factor = pointFactors_[pointI];

            // Set the algorithm refCentre / normFactor
            pointAlgorithm.setRefCentre(pIndex);
            pointAlgorithm.setNormFactor(factor);

            // Find the nearest candidates for mapping
            pointAlgorithm.findMappingCandidates(mapCandidates);

            // Normalize using sum of weights
            pointAlgorithm.normalize(true);

            // Populate lists
            pointAlgorithm.populateLists
            (
                masterObjects,
                pointCentres_[pointI],
                pointWeights_[pointI]
            );

            // Compute error
            scalar error = mag(1.0 - sum(pointWeights_[pointI]));

            if (error > matchTol)
            {
                nInconsistencies++;

                // Add to list
                pointErrors.append(error);

                // Accumulate error stats
                maxPointError = Foam::max(maxPointError, error);

                failedPoints.append(objectMap(pIndex, labelList(0)));
            }
        }
    }

    if (nInconsistencies)
    {
        Pout<< " Mapping errors: "
            << " max cell error: " << maxCellError
            << " max face error: " << maxFaceError
            << " max point error: " << maxPointError
            << endl;

        if (debug || mappingOutput)
        {
            if (failedCells.size())
            {
                Pout<< " failedCells: " << endl;

                forAll(failedCells, cellI)
                {
                    label index = failedCells[cellI].index();

                    Pout<< "  Cell: " << index
                        << "  Error: " << cellErrors[cellI]
                        << endl;
                }
            }

            if (failedFaces.size())
            {
                Pout<< " failedFaces: " << endl;

                forAll(failedFaces, faceI)
                {
                    label index = failedFaces[faceI].index();
                    label patchIndex = whichPatch(index);

                    const polyBoundaryMesh& boundary = boundaryMesh();

                    Pout<< "  Face: " << index
                        << "  Patch: " << boundary[patchIndex].name()
                        << "  Error: " << faceErrors[faceI]
                        << endl;
                }
            }

            if (failedPoints.size())
            {
                Pout<< " failedPoints: " << endl;

                forAll(failedPoints, pointI)
                {
                    label index = failedPoints[pointI].index();

                    Pout<< "  Point: " << index
                        << "  Error: " << pointErrors[pointI]
                        << endl;
                }
            }

            if (debug > 3 || mappingOutput)
            {
                // Prepare lists
                labelList objects;
                scalarField weights;
                vectorField centres;

                if (failedCells.size())
                {
                    forAll(failedCells, cellI)
                    {
                        label cIndex = failedCells[cellI].index();

                        cellAlgorithm.computeNormFactor(cIndex);

                        cellAlgorithm.findMappingCandidates(mapCandidates);

                        cellAlgorithm.computeWeights
                        (
                            cIndex,
                            0,
                            mapCandidates,
                            polyMesh::cellCells(),
                            objects,
                            weights,
                            centres,
                            true
                        );

                        computeCoupledWeights
                        (
                            cIndex,
                            cellAlgorithm.dimension(),
                            objects,
                            weights,
                            centres,
                            true
                        );

                        // Clear lists
                        objects.clear();
                        weights.clear();
                        centres.clear();
                    }
                }

                if (failedFaces.size())
                {
                    forAll(failedFaces, faceI)
                    {
                        label fIndex = failedFaces[faceI].index();
                        label patchIndex = whichPatch(fIndex);

                        const polyBoundaryMesh& boundary = boundaryMesh();

                        faceAlgorithm.computeNormFactor(fIndex);

                        faceAlgorithm.findMappingCandidates(mapCandidates);

                        faceAlgorithm.computeWeights
                        (
                            fIndex,
                            boundary[patchIndex].start(),
                            mapCandidates,
                            boundary[patchIndex].faceFaces(),
                            objects,
                            weights,
                            centres,
                            true
                        );

                        computeCoupledWeights
                        (
                            fIndex,
                            faceAlgorithm.dimension(),
                            objects,
                            weights,
                            centres,
                            true
                        );

                        // Clear lists
                        objects.clear();
                        weights.clear();
                        centres.clear();
                    }
                }
            }
        }
    }
}


// Static equivalent for multiThreading
void dynamicTopoFvMesh::computeMappingThread(void *argument)
{
    // Recast the argument
    meshHandler *thread = static_cast<meshHandler*>(argument);

    if (thread->slave())
    {
        thread->sendSignal(meshHandler::START);
    }

    dynamicTopoFvMesh& mesh = thread->reference();

    // Recast the pointers for the argument
    scalar& matchTol  = *(static_cast<scalar*>(thread->operator()(0)));
    bool& skipMapping = *(static_cast<bool*>(thread->operator()(1)));
    bool& mappingOutput = *(static_cast<bool*>(thread->operator()(2)));
    label& pointStart = *(static_cast<label*>(thread->operator()(3)));
    label& pointSize = *(static_cast<label*>(thread->operator()(4)));
    label& faceStart = *(static_cast<label*>(thread->operator()(5)));
    label& faceSize = *(static_cast<label*>(thread->operator()(6)));
    label& cellStart = *(static_cast<label*>(thread->operator()(7)));
    label& cellSize = *(static_cast<label*>(thread->operator()(8)));

    // Recast algorithms
    convexSetAlgorithm& pointAlgorithm =
    (
        *(static_cast<convexSetAlgorithm*>(thread->operator()(9)))
    );

    convexSetAlgorithm& faceAlgorithm =
    (
        *(static_cast<convexSetAlgorithm*>(thread->operator()(10)))
    );

    convexSetAlgorithm& cellAlgorithm =
    (
        *(static_cast<convexSetAlgorithm*>(thread->operator()(11)))
    );

    // Now calculate addressing
    mesh.computeMapping
    (
        matchTol,
        skipMapping,
        mappingOutput,
        pointStart, pointSize,
        faceStart, faceSize,
        cellStart, cellSize,
        pointAlgorithm,
        faceAlgorithm,
        cellAlgorithm
    );

    if (thread->slave())
    {
        thread->sendSignal(meshHandler::STOP);
    }
}


// Routine to invoke threaded mapping
void dynamicTopoFvMesh::threadedMapping
(
    scalar matchTol,
    bool skipMapping,
    bool mappingOutput
)
{
    label nThreads = threader_->getNumThreads();

    // If mapping is being skipped, issue a warning.
    if (skipMapping)
    {
        Info<< " *** Mapping is being skipped *** " << endl;
    }

    // Count pointsFromPoints
    label nPointsFromPoints = 0;
    label nPointsFromSubMeshes = 0;

    const labelPair invalidPair(-1, -1);

    forAllConstIter(Map<mapPointPair>, pointParents_, pIter)
    {
        const mapPointPair& pair = pIter();

        if (pair.second() == invalidPair)
        {
            nPointsFromPoints++;
        }
        else
        {
            nPointsFromSubMeshes++;
        }
    }

    pointFactors_.setSize(nPointsFromPoints);
    pointsFromPoints_.setSize(nPointsFromPoints);

    topoMapper::MapPointList subMeshPoints(nPointsFromSubMeshes);

    // Populate pointsFromPoints
    nPointsFromPoints = 0;
    nPointsFromSubMeshes = 0;

    forAllConstIter(Map<mapPointPair>, pointParents_, pIter)
    {
        const mapPointPair& pair = pIter();

        if (pair.second() == invalidPair)
        {
            pointFactors_[nPointsFromPoints] = pair.first();
            pointsFromPoints_[nPointsFromPoints].index() = pIter.key();
            nPointsFromPoints++;
        }
        else
        {
            topoMapper::MapPoint pMap(pIter.key(), pair.second());

            subMeshPoints[nPointsFromSubMeshes++] = pMap;
        }
    }

    // Populate facesFromFaces
    label nFacesFromFaces = 0;

    facesFromFaces_.setSize(faceParents_.size());

    forAllConstIter(labelHashSet, faceParents_, fIter)
    {
        facesFromFaces_[nFacesFromFaces++].index() = fIter.key();
    }

    // Populate cellsFromCells
    label nCellsFromCells = 0;

    cellsFromCells_.setSize(cellParents_.size());

    forAllConstIter(labelHashSet, cellParents_, cIter)
    {
        cellsFromCells_[nCellsFromCells++].index() = cIter.key();
    }

    // Set sizes for mapping
    pointWeights_.setSize(nPointsFromPoints, scalarField(0));
    pointCentres_.setSize(nPointsFromPoints, vectorField(0));
    faceWeights_.setSize(nFacesFromFaces, scalarField(0));
    faceCentres_.setSize(nFacesFromFaces, vectorField(0));
    cellWeights_.setSize(nCellsFromCells, scalarField(0));
    cellCentres_.setSize(nCellsFromCells, vectorField(0));

    // Set subMesh point mapping
    // TODO: check, TM
    //mapper_->setSubMeshMapPointList(xferMove(subMeshPoints));
    mapper_->setSubMeshMapPointList(subMeshPoints);

    // Convex-set algorithm for points
    pointSetAlgorithm pointAlgorithm
    (
        (*this),
        oldPoints_,
        edges_,
        faces_,
        cells_,
        owner_,
        neighbour_
    );

    // Convex-set algorithm for faces
    faceSetAlgorithm faceAlgorithm
    (
        (*this),
        oldPoints_,
        edges_,
        faces_,
        cells_,
        owner_,
        neighbour_
    );

    // Convex-set algorithm for cells
    cellSetAlgorithm cellAlgorithm
    (
        (*this),
        oldPoints_,
        edges_,
        faces_,
        cells_,
        owner_,
        neighbour_
    );

    if (mappingOutput)
    {
        pointAlgorithm.writeMappingCandidates();
        faceAlgorithm.writeMappingCandidates();
        cellAlgorithm.writeMappingCandidates();
    }

    // Check if single-threaded
    if (nThreads == 1)
    {
        // Force calculation of demand-driven data on subMeshes
        initCoupledWeights();

        computeMapping
        (
            matchTol,
            skipMapping,
            mappingOutput,
            0, nPointsFromPoints,
            0, nFacesFromFaces,
            0, nCellsFromCells,
            pointAlgorithm,
            faceAlgorithm,
            cellAlgorithm
        );

        return;
    }

    // Set one handler per thread
    PtrList<meshHandler> hdl(nThreads);

    forAll(hdl, i)
    {
        hdl.set(i, new meshHandler(*this, threader()));
    }

    // Simple load-balancing scheme
    FixedList<label, 3> index(-1);
    FixedList<labelList, 3> tStarts(labelList(nThreads, 0));
    FixedList<labelList, 3> tSizes(labelList(nThreads, 0));

    index[0] = nPointsFromPoints;
    index[1] = nFacesFromFaces;
    index[2] = nCellsFromCells;

    if (debug > 2)
    {
        Pout<< " Mapping Points: " << index[0] << nl
            << " Mapping Faces: " << index[1] << nl
            << " Mapping Cells: " << index[2] << endl;
    }

    forAll(index, indexI)
    {
        label j = 0, total = 0;

        while (index[indexI]--)
        {
            tSizes[indexI][(j = tSizes[indexI].fcIndex(j))]++;
        }

        for (label i = 1; i < tStarts[indexI].size(); i++)
        {
            tStarts[indexI][i] = tSizes[indexI][i-1] + total;

            total += tSizes[indexI][i-1];
        }

        if (debug > 2)
        {
            Pout<< " Load starts: " << tStarts[indexI] << nl
                << " Load sizes: " << tSizes[indexI] << endl;
        }
    }

    // Set the argument list for each thread
    forAll(hdl, i)
    {
        // Size up the argument list
        hdl[i].setSize(12);

        // Set match tolerance
        hdl[i].set(0, &matchTol);

        // Set the skipMapping flag
        hdl[i].set(1, &skipMapping);

        // Set the mappingOutput flag
        hdl[i].set(2, &mappingOutput);

        // Set the start/size indices
        hdl[i].set(3, &(tStarts[0][i]));
        hdl[i].set(4, &(tSizes[0][i]));
        hdl[i].set(5, &(tStarts[1][i]));
        hdl[i].set(6, &(tSizes[1][i]));
        hdl[i].set(7, &(tStarts[2][i]));
        hdl[i].set(8, &(tSizes[2][i]));

        // Set algorithms
        hdl[i].set(9, &(pointAlgorithm));
        hdl[i].set(10, &(faceAlgorithm));
        hdl[i].set(11, &(cellAlgorithm));
    }

    // Prior to multi-threaded operation,
    // force calculation of demand-driven data.
    polyMesh::cells();
    primitiveMesh::cellCells();

    const polyBoundaryMesh& boundary = boundaryMesh();

    forAll(boundary, patchI)
    {
        boundary[patchI].faceFaces();
    }

    // Force calculation of demand-driven data on subMeshes
    initCoupledWeights();

    // Execute threads in linear sequence
    executeThreads(identity(nThreads), hdl, &computeMappingThread);
}


// Set fill-in mapping information for a particular cell
void dynamicTopoFvMesh::setCellMapping
(
    const label cIndex
)
{
    if (debug > 3)
    {
        Pout<< "Inserting mapping cell: " << cIndex << endl;
    }

    // Update cell-parents information
    cellParents_.set(cIndex);
}


// Set fill-in mapping information for a particular face
void dynamicTopoFvMesh::setFaceMapping
(
    const label fIndex
)
{
    label patch = whichPatch(fIndex);
    label neiProc = getNeighbourProcessor(patch);

    if (debug > 3)
    {
        const polyBoundaryMesh& boundary = boundaryMesh();

        word pName;

        if (patch == -1)
        {
            pName = "Internal";
        }
        else
        if (patch < boundary.size())
        {
            pName = boundaryMesh()[patch].name();
        }
        else
        {
            pName = "New patch: " + Foam::name(patch);
        }

        Pout<< "Inserting mapping face: " << fIndex
            << " patch: " << pName
            << " neiProc: "  << neiProc
            << endl;
    }

    // For internal / processor faces, bail out
    if (patch == -1 || neiProc > -1)
    {
        return;
    }

    // Update face-parents information
    faceParents_.set(fIndex);
}


// Set fill-in mapping information for a particular point
void dynamicTopoFvMesh::setPointMapping
(
    const label pIndex,
    const mapPointPair& pair
)
{
    Map<mapPointPair>::iterator pIter = pointParents_.find(pIndex);

    // Check for existing entries
    if (pIter == pointParents_.end())
    {
        // Update point-parents information
        pointParents_.set(pIndex, pair);
    }
    else
    {
        const labelPair invalidPair(-1, -1);

        const scalar nDist = pair.first();
        const scalar pDist = pIter().first();

        const labelPair& nMapPair = pair.second();
        const labelPair& pMapPair = pIter().second();

        if (nMapPair == invalidPair && pMapPair == invalidPair)
        {
            // Update with mapping information
            // only if the search radius is greater
            if (nDist > pDist)
            {
                pIter() = pair;
            }
        }
        else
        {
            pIter() = pair;
        }
    }

    if (debug > 3)
    {
        Pout<< "Inserting mapping point: " << pIndex
            << " pair: " << pointParents_[pIndex] << endl;
    }
}


} // End namespace Foam

// ************************************************************************* //
