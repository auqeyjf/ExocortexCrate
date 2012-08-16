#include "AlembicMax.h"
#include "AlembicXformUtilities.h"
#include "AlembicMAXScript.h"
#include "AlembicMetadataUtils.h"


bool isAlembicXform( Alembic::AbcGeom::IObject *pIObj, bool& isConstant ) {
	Alembic::AbcGeom::IXform objXfrm;

	isConstant = true; 

	if(Alembic::AbcGeom::IXform::matches((*pIObj).getMetaData())) {
		objXfrm = Alembic::AbcGeom::IXform(*pIObj,Alembic::Abc::kWrapExisting);
		if( objXfrm.valid() ) {
			isConstant = objXfrm.getSchema().isConstant();
		} 
	}

	return objXfrm.valid();
}

size_t getNumXformChildren( Alembic::AbcGeom::IObject& iObj )
{
	size_t xFormChildCount = 0;
	// An XForm with xForm(s) with an xForm child will require a dummy node (and a single xform can be attached it child geometry node)
	for(size_t j=0; j<iObj.getNumChildren(); j++)
	{
		if(Alembic::AbcGeom::IXform::matches(iObj.getChild(j).getMetaData()))
		{
			xFormChildCount++;
		}
	} 
	return xFormChildCount;
}

void AlembicImport_FillInXForm_Internal(alembic_fillxform_options &options);

void AlembicImport_FillInXForm(alembic_fillxform_options &options)
{
	ESS_STRUCTURED_EXCEPTION_REPORTING_START
		AlembicImport_FillInXForm_Internal( options );
	ESS_STRUCTURED_EXCEPTION_REPORTING_END
}

void AlembicImport_FillInXForm_Internal(alembic_fillxform_options &options)
{
   if(!options.pIObj->valid())
        return;
    
    // Alembic::AbcGeom::IXform obj;
    Alembic::AbcGeom::IXform obj(*(options.pIObj), Alembic::Abc::kWrapExisting);

    if(!obj.valid())
        return;

	double SampleTime = GetSecondsFromTimeValue(options.dTicks);

	 Alembic::Abc::M44d matrix;	// constructor creates an identity matrix

   // if no time samples, default to identity matrix
   if( obj.getSchema().getNumSamples() > 0 ) {
		SampleInfo sampleInfo = getSampleInfo(
			SampleTime,
			obj.getSchema().getTimeSampling(),
			obj.getSchema().getNumSamples()
			);

		Alembic::AbcGeom::XformSample sample;
		obj.getSchema().get(sample,sampleInfo.floorIndex);
		matrix = sample.getMatrix();

		const Alembic::Abc::Box3d &box3d = sample.getChildBounds();

		options.maxBoundingBox = Box3(Point3(box3d.min.x, box3d.min.y, box3d.min.z), 
			Point3(box3d.max.x, box3d.max.y, box3d.max.z));

		// blend 
		if(sampleInfo.alpha != 0.0)
		{
			obj.getSchema().get(sample,sampleInfo.ceilIndex);
			Alembic::Abc::M44d ceilMatrix = sample.getMatrix();
			matrix = (1.0 - sampleInfo.alpha) * matrix + sampleInfo.alpha * ceilMatrix;
		}
   }

    Matrix3 objMatrix(
        Point3(matrix.getValue()[0], matrix.getValue()[1], matrix.getValue()[2]),
        Point3(matrix.getValue()[4], matrix.getValue()[5], matrix.getValue()[6]),
        Point3(matrix.getValue()[8], matrix.getValue()[9], matrix.getValue()[10]),
        Point3(matrix.getValue()[12], matrix.getValue()[13], matrix.getValue()[14]));

    ConvertAlembicMatrixToMaxMatrix(objMatrix, options.maxMatrix);

    if (options.bIsCameraTransform)
    {
        // Cameras in Max are already pointing down the negative z-axis (as is expected from Alembic).
        // So we rotate it by 90 degrees so that it is pointing down the positive y-axis.
        Matrix3 rotation(TRUE);
        rotation.RotateX(HALFPI);
        options.maxMatrix = rotation * options.maxMatrix;
    }
}

int AlembicImport_DummyNode(Alembic::AbcGeom::IObject& iObj, alembic_importoptions &options, INode** pMaxNode, const std::string& importName)
{
    Object* dObj = static_cast<Object*>(CreateInstance(HELPER_CLASS_ID, Class_ID(DUMMY_CLASS_ID,0)));
	if (!dObj){
		return alembic_failure;
	}

    DummyObject *pDummy = static_cast<DummyObject*>(dObj);

    // Hard code these values for now
    pDummy->SetColor(Point3(0.6f, 0.8f, 1.0f));
    

    double SampleTime = GetSecondsFromTimeValue( GET_MAX_INTERFACE()->GetTime() );

    Alembic::AbcGeom::IXform obj(iObj, Alembic::Abc::kWrapExisting);

	if(!obj.valid()){
		return alembic_failure;
	}

    SampleInfo sampleInfo = getSampleInfo(
        SampleTime,
        obj.getSchema().getTimeSampling(),
        obj.getSchema().getNumSamples()
        );

    Alembic::AbcGeom::XformSample sample;
    obj.getSchema().get(sample,sampleInfo.floorIndex);
    Alembic::Abc::M44d matrix = sample.getMatrix();

    const Alembic::Abc::Box3d &box3d = sample.getChildBounds();

	pDummy->SetBox( Box3(Point3(box3d.min.x, box3d.min.y, box3d.min.z), Point3(box3d.max.x, box3d.max.y, box3d.max.z)) );

    pDummy->EnableDisplay();

    *pMaxNode = GET_MAX_INTERFACE()->CreateObjectNode(dObj, EC_UTF8_to_TCHAR( importName.c_str() ) );

    SceneEntry *pEntry = options.sceneEnumProc.Append(*pMaxNode, dObj, OBTYPE_DUMMY, &std::string(iObj.getFullName())); 
    options.currentSceneList.Append(pEntry);

	importMetadata(iObj);
	
	return alembic_success;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// AlembicImport_XForm
///////////////////////////////////////////////////////////////////////////////////////////////////
int AlembicImport_XForm(INode* pParentNode, INode* pMaxNode, Alembic::AbcGeom::IObject& iObj, const std::string &file, alembic_importoptions &options)
{
	const std::string &identifier = iObj.getFullName();

	bool isConstant = false;
	if( ! isAlembicXform( &iObj, isConstant ) ) {
		return alembic_failure;
	}

	// Find out if we're dealing with a camera
	std::string modelfullid = getModelFullName(std::string(iObj.getFullName()));
	Alembic::AbcGeom::IObject iChildObj = getObjectFromArchive(file,modelfullid);
	bool bIsCamera = iChildObj.valid() && Alembic::AbcGeom::ICamera::matches(iChildObj.getMetaData());

	TimeValue zero(0);
	if(!isConstant) {

		AlembicXformController *pControl = NULL;		
		bool bCreatedController = false;

		if(options.attachToExisting){
			Animatable* pAnimatable = pMaxNode->SubAnim(2);

			if(pAnimatable && pAnimatable->ClassID() == ALEMBIC_XFORM_CONTROLLER_CLASSID){
				pControl = static_cast<AlembicXformController*>(pAnimatable);

				std::string modIdentifier = EC_MCHAR_to_UTF8( pControl->GetParamBlockByID(0)->GetStr(GetParamIdByName(pControl, 0, "identifier"), zero) );
				if(strcmp(modIdentifier.c_str(), identifier.c_str()) != 0){
					pControl = NULL;
				}
			}
		}
		if(!pControl){
			pControl = static_cast<AlembicXformController*>
				(GetCOREInterface()->CreateInstance(CTRL_MATRIX3_CLASS_ID, ALEMBIC_XFORM_CONTROLLER_CLASSID));
			bCreatedController = true;
		}

		pControl->GetParamBlockByID( 0 )->SetValue( GetParamIdByName( pControl, 0, "identifier" ), zero, EC_UTF8_to_TCHAR( identifier.c_str() ) );

		if(bCreatedController){

			pControl->GetParamBlockByID( 0 )->SetValue( GetParamIdByName( pControl, 0, "path" ), zero, EC_UTF8_to_TCHAR( file.c_str() ) );
			pControl->GetParamBlockByID( 0 )->SetValue( GetParamIdByName( pControl, 0, "time" ), zero, 0.0f );
			pControl->GetParamBlockByID( 0 )->SetValue( GetParamIdByName( pControl, 0, "camera" ), zero, ( bIsCamera ? TRUE : FALSE ) );
			pControl->GetParamBlockByID( 0 )->SetValue( GetParamIdByName( pControl, 0, "muted" ), zero, FALSE );

   			// Add the modifier to the node
			pMaxNode->SetTMController(pControl);

			GET_MAX_INTERFACE()->SelectNode( pMaxNode );
			char szControllerName[10000];	
			sprintf_s( szControllerName, 10000, "$.transform.controller.time" );
			AlembicImport_ConnectTimeControl( szControllerName, options );
		}
	}
	else{//if the transform is not animated, do not use a controller. Thus, the user will be able to adjust the object position, orientation and so on.
		
		//check if the xform controlller exists, and then delete it
		
		Control* pControl = pMaxNode->GetTMController();
		
		if(pControl){
			MSTR name;
			pControl->GetClassName(name);
			if(strcmp( EC_MSTR_to_UTF8( name ).c_str(), "Position/Rotation/Scale") != 0){
				//TODO: Do I need to be concerned with deleting controllers?
				pMaxNode->SetTMController(CreatePRSControl());
			}
		}

		//doesn't work for some reason
		//Animatable* pAnimatable = pMaxNode->SubAnim(2);
		//if(pAnimatable && pMaxNode->CanDeleteSubAnim(2) ){
		//	pMaxNode->DeleteSubAnim(2);
		//}
		
		alembic_fillxform_options xformOptions;
		xformOptions.pIObj = &iObj;
		xformOptions.dTicks = 0;
		xformOptions.bIsCameraTransform = bIsCamera;

		AlembicImport_FillInXForm(xformOptions);

		if(pParentNode != NULL){
			pMaxNode->SetNodeTM(zero, xformOptions.maxMatrix * pParentNode->GetObjectTM(zero));
		}
		else{
			pMaxNode->SetNodeTM(zero, xformOptions.maxMatrix);
		}
	}
    pMaxNode->InvalidateTreeTM();
	
	if(!isConstant){
		// Lock the transform
		LockNodeTransform(pMaxNode, true);
	}

	return alembic_success;
}


