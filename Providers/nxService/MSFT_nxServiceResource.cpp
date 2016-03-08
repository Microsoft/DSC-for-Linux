/* @migen@ */
#include "MSFT_nxServiceResource.h"


#include "debug_tags.hpp"
#include "MI.h"
#include "PythonProvider.hpp"


#include <cstdlib>


typedef struct _MSFT_nxServiceResource_Self : public scx::PythonProvider
{
    /*ctor*/ _MSFT_nxServiceResource_Self ()
        : scx::PythonProvider ("nxService")
    {
        // empty
    }
} MSFT_nxServiceResource_Self;


void MI_CALL MSFT_nxServiceResource_Load(
    _Outptr_result_maybenull_ MSFT_nxServiceResource_Self** self,
    _In_opt_ MI_Module_Self* selfModule,
    _In_ MI_Context* context)
{
    SCX_BOOKEND_EX ("Load", " name=\"nxService\"");
    MI_UNREFERENCED_PARAMETER(selfModule);
    MI_Result res = MI_RESULT_OK;
    if (0 != self)
    {
        if (0 == *self)
        {
            *self = new MSFT_nxServiceResource_Self;
            if (EXIT_SUCCESS != (*self)->init ())
            {
                delete *self;
                *self = 0;
                res = MI_RESULT_FAILED;
            }
        }
    }
    else
    {
        res = MI_RESULT_FAILED;
    }
    MI_Context_PostResult(context, res);
}

void MI_CALL MSFT_nxServiceResource_Unload(
    _In_opt_ MSFT_nxServiceResource_Self* self,
    _In_ MI_Context* context)
{
    SCX_BOOKEND_EX ("Unload", " name=\"nxService\"");
    if (self)
    {
        delete self;
    }
    MI_Context_PostResult(context, MI_RESULT_OK);
}

void MI_CALL MSFT_nxServiceResource_EnumerateInstances(
    _In_opt_ MSFT_nxServiceResource_Self* self,
    _In_ MI_Context* context,
    _In_opt_z_ const MI_Char* nameSpace,
    _In_opt_z_ const MI_Char* className,
    _In_opt_ const MI_PropertySet* propertySet,
    _In_ MI_Boolean keysOnly,
    _In_opt_ const MI_Filter* filter)
{
    MI_UNREFERENCED_PARAMETER(self);
    MI_UNREFERENCED_PARAMETER(nameSpace);
    MI_UNREFERENCED_PARAMETER(className);
    MI_UNREFERENCED_PARAMETER(propertySet);
    MI_UNREFERENCED_PARAMETER(keysOnly);
    MI_UNREFERENCED_PARAMETER(filter);

    MI_Context_PostResult(context, MI_RESULT_NOT_SUPPORTED);
}

void MI_CALL MSFT_nxServiceResource_GetInstance(
    _In_opt_ MSFT_nxServiceResource_Self* self,
    _In_ MI_Context* context,
    _In_opt_z_ const MI_Char* nameSpace,
    _In_opt_z_ const MI_Char* className,
    _In_ const MSFT_nxServiceResource* instanceName,
    _In_opt_ const MI_PropertySet* propertySet)
{
    MI_UNREFERENCED_PARAMETER(self);
    MI_UNREFERENCED_PARAMETER(nameSpace);
    MI_UNREFERENCED_PARAMETER(className);
    MI_UNREFERENCED_PARAMETER(instanceName);
    MI_UNREFERENCED_PARAMETER(propertySet);

    MI_Context_PostResult(context, MI_RESULT_NOT_SUPPORTED);
}

void MI_CALL MSFT_nxServiceResource_CreateInstance(
    _In_opt_ MSFT_nxServiceResource_Self* self,
    _In_ MI_Context* context,
    _In_opt_z_ const MI_Char* nameSpace,
    _In_opt_z_ const MI_Char* className,
    _In_ const MSFT_nxServiceResource* newInstance)
{
    MI_UNREFERENCED_PARAMETER(self);
    MI_UNREFERENCED_PARAMETER(nameSpace);
    MI_UNREFERENCED_PARAMETER(className);
    MI_UNREFERENCED_PARAMETER(newInstance);

    MI_Context_PostResult(context, MI_RESULT_NOT_SUPPORTED);
}

void MI_CALL MSFT_nxServiceResource_ModifyInstance(
    _In_opt_ MSFT_nxServiceResource_Self* self,
    _In_ MI_Context* context,
    _In_opt_z_ const MI_Char* nameSpace,
    _In_opt_z_ const MI_Char* className,
    _In_ const MSFT_nxServiceResource* modifiedInstance,
    _In_opt_ const MI_PropertySet* propertySet)
{
    MI_UNREFERENCED_PARAMETER(self);
    MI_UNREFERENCED_PARAMETER(nameSpace);
    MI_UNREFERENCED_PARAMETER(className);
    MI_UNREFERENCED_PARAMETER(modifiedInstance);
    MI_UNREFERENCED_PARAMETER(propertySet);

    MI_Context_PostResult(context, MI_RESULT_NOT_SUPPORTED);
}

void MI_CALL MSFT_nxServiceResource_DeleteInstance(
    _In_opt_ MSFT_nxServiceResource_Self* self,
    _In_ MI_Context* context,
    _In_opt_z_ const MI_Char* nameSpace,
    _In_opt_z_ const MI_Char* className,
    _In_ const MSFT_nxServiceResource* instanceName)
{
    MI_UNREFERENCED_PARAMETER(self);
    MI_UNREFERENCED_PARAMETER(nameSpace);
    MI_UNREFERENCED_PARAMETER(className);
    MI_UNREFERENCED_PARAMETER(instanceName);

    MI_Context_PostResult(context, MI_RESULT_NOT_SUPPORTED);
}

void MI_CALL MSFT_nxServiceResource_Invoke_GetTargetResource(
    _In_opt_ MSFT_nxServiceResource_Self* self,
    _In_ MI_Context* context,
    _In_opt_z_ const MI_Char* nameSpace,
    _In_opt_z_ const MI_Char* className,
    _In_opt_z_ const MI_Char* methodName,
    _In_ const MSFT_nxServiceResource* instanceName,
    _In_opt_ const MSFT_nxServiceResource_GetTargetResource* in)
{
    SCX_BOOKEND_EX ("Get", " name=\"nxService\"");
    MI_Result result = MI_RESULT_FAILED;
    if (self)
    {
        MI_Instance* retInstance;
        MI_Instance_Clone (&in->InputResource.value->__instance, &retInstance);
        result = self->get (in->InputResource.value->__instance, context,
                            retInstance);
        if (MI_RESULT_OK == result)
        {
            SCX_BOOKEND_PRINT ("packing succeeded!");
            MSFT_nxServiceResource_GetTargetResource out;
            MSFT_nxServiceResource_GetTargetResource_Construct (&out, context);
            MSFT_nxServiceResource_GetTargetResource_Set_MIReturn (&out, 0);
            MI_Value value;
            value.instance = retInstance;
            MI_Instance_SetElement (&out.__instance, "OutputResource", &value,
                                    MI_INSTANCE, 0);
            result = MSFT_nxServiceResource_GetTargetResource_Post (&out, context);
            if (MI_RESULT_OK != result)
            {
                SCX_BOOKEND_PRINT ("post Failed");
            }
            MSFT_nxServiceResource_GetTargetResource_Destruct (&out);
        }
        else
        {
            SCX_BOOKEND_PRINT ("get FAILED");
        }
        MI_Instance_Delete (retInstance);
    }
    MI_Context_PostResult (context, result);
}

void MI_CALL MSFT_nxServiceResource_Invoke_InventoryTargetResource(
    _In_opt_ MSFT_nxServiceResource_Self* self,
    _In_ MI_Context* context,
    _In_opt_z_ const MI_Char* nameSpace,
    _In_opt_z_ const MI_Char* className,
    _In_opt_z_ const MI_Char* methodName,
    _In_ const MSFT_nxServiceResource* instanceName,
    _In_opt_ const MSFT_nxServiceResource_InventoryTargetResource* in)
{
    SCX_BOOKEND_EX ("Inventory", " name=\"nxService\"");
    MI_Result result = MI_RESULT_FAILED;
    if (self)
    {
        MI_Instance* retInstance;
	MI_NewDynamicInstance (
                context, className,
                NULL, &retInstance);
        result = self->inventory (in->InputResource.value->__instance, context,
                            retInstance);
        if (MI_RESULT_OK == result)
        {
            SCX_BOOKEND_PRINT ("packing succeeded!");
            MSFT_nxServiceResource_InventoryTargetResource out;
            MSFT_nxServiceResource_InventoryTargetResource_Construct (&out, context);
            MSFT_nxServiceResource_InventoryTargetResource_Set_MIReturn (&out, 0);

            MI_Value value, value_inventory;
	    result = MI_Instance_GetElement(retInstance, "__Inventory", &value_inventory, NULL, NULL, NULL);
	    if (MI_RESULT_OK != result)
	    {
		SCX_BOOKEND_PRINT ("unable to find __Inventory instance");
		MSFT_nxServiceResource_InventoryTargetResource_Destruct (&out);
		MI_Context_PostResult(context, MI_RESULT_NOT_FOUND);
	    }

            value.instancea = value_inventory.instancea;
            result = MI_Instance_SetElement (&out.__instance, "inventory", &value,
                                    MI_INSTANCEA, 0);
	    if (MI_RESULT_OK != result)
	    {
		SCX_BOOKEND_PRINT ("unable to set 'inventory' in out instance");
		MSFT_nxServiceResource_InventoryTargetResource_Destruct (&out);
		MI_Context_PostResult(context, MI_RESULT_FAILED);
	    }

            result = MSFT_nxServiceResource_InventoryTargetResource_Post (&out, context);
            if (MI_RESULT_OK != result)
            {
                SCX_BOOKEND_PRINT ("post Failed");
            }
            MSFT_nxServiceResource_InventoryTargetResource_Destruct (&out);
        }
        else
        {
            SCX_BOOKEND_PRINT ("inventory FAILED");
        }
        MI_Instance_Delete (retInstance);
    }
    MI_Context_PostResult (context, result);
}

void MI_CALL MSFT_nxServiceResource_Invoke_TestTargetResource(
    _In_opt_ MSFT_nxServiceResource_Self* self,
    _In_ MI_Context* context,
    _In_opt_z_ const MI_Char* nameSpace,
    _In_opt_z_ const MI_Char* className,
    _In_opt_z_ const MI_Char* methodName,
    _In_ const MSFT_nxServiceResource* instanceName,
    _In_opt_ const MSFT_nxServiceResource_TestTargetResource* in)
{
    MI_Result result = MI_RESULT_FAILED;
    if (self)
    {
        MI_Boolean testResult = MI_FALSE;
        result = self->test (in->InputResource.value->__instance, &testResult);
        if (MI_RESULT_OK == result)
        {
            MSFT_nxServiceResource_TestTargetResource out;
            MSFT_nxServiceResource_TestTargetResource_Construct (&out, context);
            MSFT_nxServiceResource_TestTargetResource_Set_Result (
                &out, testResult);
            MSFT_nxServiceResource_TestTargetResource_Set_MIReturn (&out, 0);
            MSFT_nxServiceResource_TestTargetResource_Post (&out, context);
            MSFT_nxServiceResource_TestTargetResource_Destruct (&out);
        }
    }
    MI_Context_PostResult (context, result);
}

void MI_CALL MSFT_nxServiceResource_Invoke_SetTargetResource(
    _In_opt_ MSFT_nxServiceResource_Self* self,
    _In_ MI_Context* context,
    _In_opt_z_ const MI_Char* nameSpace,
    _In_opt_z_ const MI_Char* className,
    _In_opt_z_ const MI_Char* methodName,
    _In_ const MSFT_nxServiceResource* instanceName,
    _In_opt_ const MSFT_nxServiceResource_SetTargetResource* in)
{
    MI_Result result = MI_RESULT_FAILED;
    if (self)
    {
        MI_Result setResult = MI_RESULT_FAILED;
        result = self->set (in->InputResource.value->__instance, &setResult);
        if (MI_RESULT_OK == result)
        {
            result = setResult;
            MSFT_nxServiceResource_SetTargetResource out;
            MSFT_nxServiceResource_SetTargetResource_Construct (&out, context);
            MSFT_nxServiceResource_SetTargetResource_Set_MIReturn (
                &out, setResult);
            MSFT_nxServiceResource_SetTargetResource_Post (&out, context);
            MSFT_nxServiceResource_SetTargetResource_Destruct (&out);
        }
    }
    MI_Context_PostResult (context, result);
}
