#pragma once

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QWidget>
#include <functional>

class CommandRegistry {
  public:
    explicit CommandRegistry(QWidget *owner) : owner_(owner) {}

    QAction *add(const QString &id, const QString &label, const QKeySequence &shortcut,
                 std::function<void()> callback) {
        auto *action = new QAction(label, owner_);
        action->setObjectName(id);
        action->setShortcut(shortcut);
        owner_->addAction(action);
        QObject::connect(action, &QAction::triggered, owner_, std::move(callback));
        actions_.insert(id.toCaseFolded(), action);
        return action;
    }

    void alias(const QString &id, QAction *action) { actions_.insert(id.toCaseFolded(), action); }

    bool addUserAlias(const QString &id, QAction *action) {
        const auto key = id.toCaseFolded();
        if (actions_.contains(key) || key == "cd")
            return false;
        userAliases_.insert(key, action);
        return true;
    }
    QStringList ids() const {
        auto result = actions_.keys();
        result.append(userAliases_.keys());
        result.removeDuplicates();
        result.sort();
        return result;
    }
    void removeAlias(const QString &id) { userAliases_.remove(id.toCaseFolded()); }

    QAction *get(const QString &id) const {
        return userAliases_.value(id.toCaseFolded(), actions_.value(id.toCaseFolded()));
    }

  private:
    QWidget *owner_;
    QHash<QString, QAction *> actions_, userAliases_;
};
