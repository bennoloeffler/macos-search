#ifndef FILESEARCHUITEST_H
#define FILESEARCHUITEST_H

#include <QObject>

class FileSearchUiTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    void testMinimumWidthIs820();
    void testModeTogglesExistAndPersist();
    void testDefaultModeIsBoth();
    void testContentFieldExists();
    void testContentRegexCheckboxExists();
    void testContentHelpButtonExists();
    void testContentFieldDisabledByDefault();
    void testContentFieldDisabledWhenAboveThreshold();
    void testHelpHintUpdatesWithMode();
    void testHelpLineMentionsAltEnter();
    void testTwoTabExcludeDialog();
    void testTreeFilterFoldersModeExcludesFiles();
    void testTreeFilterFilesModeIncludesFiles();
    void testTreeFilterBothModeIncludesFiles();
    void testTreeFilterRespectsShowHidden();
    void testScanHereButtonExistsAndStartsEnabled();
    void testScanHereButtonLabelDefault();
    void testScanHereButtonDisabledWhenPathDoesNotExist();
    void testInlineIndicatorLivesInsidePathField();
    void testNoStandaloneScanHereButtonAnymore();
    void testFavoriteRowsHaveIndicator();
};

#endif // FILESEARCHUITEST_H
