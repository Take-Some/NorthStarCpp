// Dedicated native GPU acceptance host.
// Intentionally does not create QtEditorWindow or start background asset scans.
#include <QApplication>
#include <QCoreApplication>
#include <iostream>
int run_material_function_smoke();
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName("NorthStar Material Function GPU QA");
    app.setOrganizationName("Kayla's Systems");
    return run_material_function_smoke();
}
