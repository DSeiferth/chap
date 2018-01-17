#include "io/wavefront_mtl_io.hpp"


/*!
 * Constructs a material with the given name.
 */
WavefrontMtlMaterial::WavefrontMtlMaterial(std::string name)
    : name_(name)
    , ambientColour_(gmx::RVec(0.5, 0.5, 0.5))
    , diffuseColour_(gmx::RVec(0.5, 0.5, 0.5))
    , specularColour_(gmx::RVec(0.5, 0.5, 0.5))
{

}


/*!
 *  Sets the materials ambient colour in RGB coordinates.
 */
void
WavefrontMtlMaterial::setAmbientColour(gmx::RVec col)
{
    ambientColour_ = col;
}


/*!
 *  Sets the materials diffuse colour in RGB coordinates.
 */
void
WavefrontMtlMaterial::setDiffuseColour(gmx::RVec col)
{
    diffuseColour_ = col;
}


/*!
 *  Sets the materials specular colour in RGB coordinates.
 */
void
WavefrontMtlMaterial::setSpecularColour(gmx::RVec col)
{
    specularColour_ = col;
}


/*!
 * Adds a material to the internal list of materials.
 */
void
WavefrontMtlObject::addMaterial(WavefrontMtlMaterial material)
{
    materials_.push_back(material);
}


/*
 *
 */
void
WavefrontMtlExporter::write(std::string fileName, WavefrontMtlObject object)
{
    file_.open(fileName.c_str(), std::fstream::out);
    
    // loop over materials:
    for(auto material : object.materials_)
    {
        // write material specifications to file:
        writeMaterialName(material.name_);
        writeAmbientColour(material.ambientColour_);
        writeDiffuseColour(material.diffuseColour_);
        writeSpecularColour(material.specularColour_);
    }

    file_.close();
}


/*!
 * Writes material name to MTL file.
 */
void
WavefrontMtlExporter::writeMaterialName(std::string name)
{
    file_<<"newmtl "<<name<<std::endl;
}


/*!
 * Writes ambient colour to MTL file.
 */
void
WavefrontMtlExporter::writeAmbientColour(const gmx::RVec &col)
{
    file_<<"Ka "<<col[XX]<<" "<<col[YY]<<" "<<col[ZZ]<<std::endl;
}


/*!
 * Writes diffuse colour to MTL file.
 */
void
WavefrontMtlExporter::writeDiffuseColour(const gmx::RVec &col)
{
    file_<<"Kd "<<col[XX]<<" "<<col[YY]<<" "<<col[ZZ]<<std::endl;
}


/*!
 * Writes specular colour to MTL file.
 */
void
WavefrontMtlExporter::writeSpecularColour(const gmx::RVec &col)
{
    file_<<"Ks "<<col[XX]<<" "<<col[YY]<<" "<<col[ZZ]<<std::endl;
}
