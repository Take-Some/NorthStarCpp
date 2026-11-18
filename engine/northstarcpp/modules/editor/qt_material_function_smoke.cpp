// Qt + GPU Material Function end-to-end regression. Creates an isolated
// function source, validates visual pins and stages the expanded GPU material.
#include "qt_material_graph_editor.hpp"
#include "qt_material_gpu_preview.hpp"
#include "qt_material_authoring.hpp"
#include "qt_editor_viewport.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/material.hpp>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QGraphicsView>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int run_material_function_smoke() {
    namespace ma=northstarcpp::editor::material_authoring;
    try {
        QTemporaryDir dir("northstar-material-function-XXXXXX");
        if(!dir.isValid())throw std::runtime_error("Cannot create project root");
        auto const root=std::filesystem::u8path(
            dir.path().toUtf8().toStdString());
        QDir().mkpath(dir.path()+"/MaterialFunctions");
        QJsonObject definition{
            {"schema","northstar.material-function.v1"},
            {"name","Multiply For GPU"},
            {"inputs",QJsonArray{
                QJsonObject{{"name","color"},{"type","vector4"}},
                QJsonObject{{"name","tint"},{"type","vector4"}}
            }},
            {"outputs",QJsonArray{
                QJsonObject{{"name","result"},{"type","vector4"},{"node","mul"}}
            }},
            {"nodes",QJsonArray{
                QJsonObject{{"id","in_color"},{"op","function_input"},{"name","color"}},
                QJsonObject{{"id","in_tint"},{"op","function_input"},{"name","tint"}},
                QJsonObject{{"id","mul"},{"op","multiply"},{"a","in_color"},{"b","in_tint"}}
            }}
        };
        QFile function(dir.path()+"/MaterialFunctions/Multiply.material-function.json");
        if(!function.open(QIODevice::WriteOnly|QIODevice::Truncate))
            throw std::runtime_error("Cannot save function fixture");
        auto bytes=QJsonDocument(definition).toJson(QJsonDocument::Compact);
        if(function.write(bytes)!=bytes.size())
            throw std::runtime_error("Cannot write function fixture");
        function.close();
        if(!ma::list_material_functions(root).contains(
            "MaterialFunctions/Multiply.material-function.json"))
            throw std::runtime_error("Function not enumerated in authoring palette");

        QJsonObject graph{
            {"nodes",QJsonArray{
                QJsonObject{{"id","color"},{"op","parameter"},{"parameter","base_color"},
                            {"x",-350},{"y",40}},
                QJsonObject{{"id","tint"},{"op","color"},
                            {"value",QJsonArray{0.8,0.7,0.6,1.0}},{"x",-350},{"y",200}},
                QJsonObject{{"id","call"},{"op","function"},
                            {"path","MaterialFunctions/Multiply.material-function.json"},
                            {"output","result"},
                            {"inputs",QJsonObject{{"color","color"},{"tint","tint"}}},
                            {"x",50},{"y",60}}
            }},
            {"outputs",QJsonObject{{"base_color","call"}}}
        };
        northstarcpp::editor::MaterialGraphEditor editor(graph,nullptr,root);
        QString reason;
        if(!editor.validate(&reason))
            throw std::runtime_error("GUI graph rejected function: "+reason.toStdString());
        auto* canvas=editor.findChild<QGraphicsView*>("materialGraphCanvas");
        if(!canvas)
            throw std::runtime_error("Material Function visual graph missing");
        if(!editor.graph().value("nodes").toArray().size())
            throw std::runtime_error("Material Function node not serializable");
        QJsonObject material{{"schema",1},{"name","FunctionGpuAcceptance"},
             {"shader","standard_lit"},{"base_color","#c0aabb"},
             {"metallic",0.2},{"roughness",0.5},{"graph",graph}};
        ma::save_descriptor(root,"Materials/FunctionGpu.ymat",material);
        auto decoded=ma::load_descriptor(root,"Materials/FunctionGpu.ymat");
        if(!decoded.value("graph").isObject())
            throw std::runtime_error("Saved function call lost");
        northstarcpp::AssetStore assets;
        assets.mount_directory("",root);
        auto loaded=northstarcpp::load_material_definition(
            assets,"Materials/FunctionGpu.ymat");
        if(loaded.graph_shader_defines.find("NS_GRAPH_BASE")==std::string::npos)
            throw std::runtime_error("Runtime VFS did not link Material Function");

        northstarcpp::editor::MaterialGpuPreview preview(root);
        preview.resize(640,600);
        preview.show();
        if(!preview.stageForTest(material,&reason))
            throw std::runtime_error("GPU function staging rejected: "+reason.toStdString());
        preview.setMaterial(material);
        QCoreApplication::processEvents();
        preview.start();
        QCoreApplication::processEvents();
        if(!preview.gpuActive())
            throw std::runtime_error("GPU material function surface unavailable");
        auto* gpu=preview.findChild<northstarcpp::editor::QtEditorViewport*>(
            "materialGpuPreviewViewport");
        if(!gpu||!gpu->renderGpuNow())
            throw std::runtime_error("Native GPU draw of function material failed");
        auto persist=[&](QByteArray const& content) {
            QSaveFile output(dir.path()+"/MaterialFunctions/Multiply.material-function.json");
            if(!output.open(QIODevice::WriteOnly) ||
               output.write(content)!=content.size() || !output.commit())
                throw std::runtime_error("Cannot atomically edit Material Function QA fixture");
        };
        QByteArray invalid=bytes;
        invalid.replace("\"type\":\"vector4\"","\"type\":\"scalar\"");
        if(invalid==bytes)
            throw std::runtime_error("Failed to generate invalid Material Function");
        persist(invalid);
        QElapsedTimer timer;
        timer.start();
        while(!preview.diagnostic().contains("Graph compiler rejected") &&
              timer.elapsed()<8000) {
            QCoreApplication::processEvents();
            QThread::msleep(12);
        }
        if(!preview.diagnostic().contains("Graph compiler rejected") ||
           !preview.gpuActive())
            throw std::runtime_error("Function edit did not report an error "
                                     "while preserving the active GPU preview");
        persist(bytes);
        timer.restart();
        while(!preview.diagnostic().contains("Live GPU PBR preview") &&
              timer.elapsed()<8000) {
            QCoreApplication::processEvents();
            QThread::msleep(12);
        }
        if(!preview.diagnostic().contains("Live GPU PBR preview") ||
           !gpu->renderGpuNow())
            throw std::runtime_error("Material Function preview did not recover "
                                     "after correcting a shader error");
        std::cout<<"Material Function Qt/VFS/GPU hot-reload and recovery: PASS\n";
        return 0;
    }catch(std::exception const& e) {
        std::cerr<<"Material Function Qt/VFS/GPU staging: FAIL: "
                 <<e.what()<<"\n";
        return 1;
    }
}
